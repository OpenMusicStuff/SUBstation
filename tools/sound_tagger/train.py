"""Trains the sound tagger on the fingerprints of labelled sample libraries, measures it, and writes the model
the intelligence module compiles in (intelligence/src/tagging/TaggerModel.inc).

    python -I train.py --work <work folder> [--out ../../intelligence/src/tagging/TaggerModel.inc] [--report report.json]

<work> is corpora.py's (labels.json) with feat/<corpus>.bin from sound_features_dump (README.md). The model
reads Essentia's descriptors of a sound (EssentiaExtractor's 81, less the file's length) and gives:

    kind      what the sound is: one of KINDS (corpora.py), softmax
    loop      whether it is a loop (several hits, a phrase) or a one-shot
    source    acoustic, electric or synthesized (NSynth's instrument sources; TinySOL's are acoustic)
    quality   NSynth's ten qualities (bright, dark, distortion, ...), each a sigmoid
    embedding a few numbers where sounds of a kind lie together: the similarity's Embedding aspect

It is an ensemble of small MLPs (ReLU), trained with every head's loss masked where a corpus has no label,
classes balanced, noise on the inputs; temperature-scaled on held-out folds. How well it does is measured
twice: grouped cross-validation (whole drum machines, whole instruments held out), and on Dirt-Samples, a
library it never sees (its near-duplicates of the drum machines' sounds left out of training first).
"""

import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, str(Path(__file__).resolve().parent))
from corpora import KINDS, DRUM_KINDS, QUALITIES, SOURCES, WORDS  # noqa: E402

WORD_NAMES = list(WORDS)

DIMS = 81
LENGTH = 47          # feature::Length: the file's length, left out (NSynth's notes are all 4 s long)
PITCH_CONF, PITCH = 44, 45
INPUTS = [d for d in range(DIMS) if d != LENGTH]
EMBED = 32
HIDDEN = (192, 96)
MEMBERS = 5


def read_features(path):
    out = {}
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"SIMBENC2", path
    dims = struct.unpack("<I", data[16:20])[0]
    pos = 20
    while pos < len(data):
        n = struct.unpack("<I", data[pos:pos + 4])[0]
        p = data[pos + 4:pos + 4 + n].decode("utf-8", "surrogateescape")
        ok = data[pos + 4 + n]
        v = np.frombuffer(data, "<f4", dims, pos + 5 + n)
        pos += 5 + n + 4 * dims
        if ok and np.all(np.isfinite(v)):
            out[p] = v.copy()
    return out


class Net(nn.Module):
    def __init__(self, inputs):
        super().__init__()
        self.trunk = nn.Sequential(nn.Linear(inputs, HIDDEN[0]), nn.ReLU(), nn.Dropout(0.2),
                                   nn.Linear(HIDDEN[0], HIDDEN[1]), nn.ReLU(), nn.Dropout(0.1),
                                   nn.Linear(HIDDEN[1], EMBED))
        self.kind = nn.Linear(EMBED, len(KINDS))
        self.loop = nn.Linear(EMBED, 1)
        self.source = nn.Linear(EMBED, len(SOURCES))
        self.quality = nn.Linear(EMBED, len(QUALITIES))
        self.words = nn.Linear(EMBED, len(WORD_NAMES))

    def forward(self, x):
        e = torch.relu(self.trunk(x))
        return e, self.kind(e), self.loop(e)[:, 0], self.source(e), self.quality(e), self.words(e)


class Data:
    """The rows: standardized inputs and each head's targets (-1 / NaN where unknown)."""

    def __init__(self, rows, labels):
        self.paths = [p for p, _ in rows]
        self.raw = np.stack([v for _, v in rows]).astype(np.float32)
        lab = [labels[p] for p in self.paths]
        self.corpus = np.array([l["corpus"] for l in lab])
        self.group = np.array([l["corpus"] + ":" + l["group"] for l in lab])
        self.kind = np.array([KINDS.index(l["kind"]) if l["kind"] in KINDS else -1 for l in lab])
        self.loop = np.array([1.0 if l["loop"] else (0.0 if l["kind"] in KINDS else np.nan) for l in lab])
        self.source = np.array([SOURCES.index(l["source"]) if l["source"] in SOURCES else -1 for l in lab])
        self.quality = np.array([l["qualities"] if l["qualities"] else [np.nan] * len(QUALITIES) for l in lab],
                                dtype=np.float32)
        self.pitch = np.array([l["pitch"] if l["pitch"] is not None else -1 for l in lab])
        self.words = np.array([[float(w in l["words"]) for w in WORD_NAMES] if l.get("words") is not None
                               else [np.nan] * len(WORD_NAMES) for l in lab], dtype=np.float32)

    def take(self, idx):
        d = object.__new__(Data)
        for k, v in self.__dict__.items():
            d.__dict__[k] = v[idx] if isinstance(v, np.ndarray) else [v[i] for i in idx]
        return d

    def __len__(self):
        return len(self.raw)


class Scaler:
    """Each input held to its training set's 0.5th and 99.5th percentiles, then standardized."""

    def fit(self, raw):
        x = raw[:, INPUTS]
        self.low = np.percentile(x, 0.5, axis=0).astype(np.float32)
        self.high = np.percentile(x, 99.5, axis=0).astype(np.float32)
        c = np.clip(x, self.low, self.high)
        self.mean = c.mean(axis=0).astype(np.float32)
        self.std = np.maximum(c.std(axis=0), 1e-3).astype(np.float32)
        return self

    def __call__(self, raw):
        return ((np.clip(raw[:, INPUTS], self.low, self.high) - self.mean) / self.std).astype(np.float32)


def weights_of(d):
    """Each row's weight: kinds balanced (a kind's rows weigh 1/sqrt(its count)), loops as much as one-shots."""
    w = np.ones(len(d), np.float32)
    counts = np.bincount(d.kind[d.kind >= 0], minlength=len(KINDS)).astype(np.float64)
    for k in range(len(KINDS)):
        if counts[k]:
            w[d.kind == k] = 1.0 / math.sqrt(counts[k])
    m = d.kind >= 0
    w[m] *= m.sum() / w[m].sum()
    loops = d.loop == 1
    if loops.any():
        w[loops] = 1.0 * m.sum() / max(loops.sum(), 1) * 0.25
    return w


def train_member(x, d, seed, epochs=60):
    torch.manual_seed(seed)
    rng = np.random.default_rng(seed)
    net = Net(x.shape[1])
    opt = torch.optim.AdamW(net.parameters(), lr=2e-3, weight_decay=1e-3)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=3e-3, total_steps=epochs * math.ceil(len(x) / 256))
    X = torch.tensor(x)
    kind = torch.tensor(d.kind)
    loop = torch.tensor(d.loop, dtype=torch.float32)
    source = torch.tensor(d.source)
    quality = torch.tensor(d.quality)
    words = torch.tensor(d.words)
    known = ~torch.isnan(words[:, 0])
    # Each word's positives weigh as much as its negatives (most sounds aren't tagged with most words).
    pos = torch.nan_to_num(words[known]).mean(0).clamp(1e-3, 0.5)
    word_pos_weight = (1 - pos) / pos
    w = torch.tensor(weights_of(d))
    ce = nn.CrossEntropyLoss(reduction="none", label_smoothing=0.05)
    bce = nn.BCEWithLogitsLoss(reduction="none")
    for _ in range(epochs):
        net.train()
        order = torch.tensor(rng.permutation(len(x)))
        for b in range(0, len(x), 256):
            i = order[b:b + 256]
            xb = X[i] + 0.15 * torch.randn(len(i), X.shape[1])
            xb = xb * (torch.rand_like(xb) > 0.05)
            _, kl, ll, sl, ql, wl = net(xb)
            loss = 0.0
            m = kind[i] >= 0
            if m.any():
                loss = loss + (ce(kl[m], kind[i][m]) * w[i][m]).sum() / w[i][m].sum()
            m = ~torch.isnan(loop[i])
            if m.any():
                loss = loss + 0.5 * (bce(ll[m], loop[i][m]) * w[i][m]).sum() / w[i][m].sum()
            m = source[i] >= 0
            if m.any():
                loss = loss + 0.3 * ce(sl[m], source[i][m]).mean()
            q = quality[i]
            m = ~torch.isnan(q[:, 0])
            if m.any():
                loss = loss + 0.3 * bce(ql[m], q[m]).mean()
            m = known[i]
            if m.any():
                wt = words[i][m]
                weight = 1 + (word_pos_weight - 1) * wt
                loss = loss + 0.5 * (nn.functional.binary_cross_entropy_with_logits(wl[m], wt, reduction="none") *
                                     weight).mean() / weight.mean()
            opt.zero_grad()
            loss.backward()
            opt.step()
            sched.step()
    net.eval()
    return net


class Ensemble:
    def __init__(self, d, members=MEMBERS, seed=0, epochs=60):
        self.scaler = Scaler().fit(d.raw)
        x = self.scaler(d.raw)
        self.nets = [train_member(x, d, seed * 100 + m, epochs) for m in range(members)]
        self.temperature = 1.0

    @torch.no_grad()
    def outputs(self, raw):
        x = torch.tensor(self.scaler(raw))
        outs = [net(x) for net in self.nets]
        kind = np.mean([o[1].numpy() for o in outs], axis=0)  # mean logits
        loop = np.mean([torch.sigmoid(o[2]).numpy() for o in outs], axis=0)
        source = np.mean([torch.softmax(o[3], 1).numpy() for o in outs], axis=0)
        quality = np.mean([torch.sigmoid(o[4]).numpy() for o in outs], axis=0)
        words = np.mean([torch.sigmoid(o[5]).numpy() for o in outs], axis=0)
        embedding = outs[0][0].numpy()
        return {"kind_logits": kind, "loop": loop, "source": source, "quality": quality, "words": words,
                "embedding": embedding}

    def kind_probs(self, logits):
        z = logits / self.temperature
        z = z - z.max(axis=1, keepdims=True)
        e = np.exp(z)
        return e / e.sum(axis=1, keepdims=True)


def fit_temperature(logits, kind):
    m = kind >= 0
    best, bt = 1e9, 1.0
    for t in np.exp(np.linspace(math.log(0.3), math.log(5), 200)):
        z = logits[m] / t
        z = z - z.max(1, keepdims=True)
        nll = -(z[np.arange(m.sum()), kind[m]] - np.log(np.exp(z).sum(1))).mean()
        if nll < best:
            best, bt = nll, t
    return float(bt)


def metrics(pred, true, prob=None, threshold=0.0):
    m = true >= 0
    out = {"n": int(m.sum())}
    if not m.any():
        return out
    p, t = pred[m], true[m]
    out["accuracy"] = float((p == t).mean())
    f1s = []
    for k in np.unique(t):
        tp = ((p == k) & (t == k)).sum()
        fp = ((p == k) & (t != k)).sum()
        fn = ((p != k) & (t == k)).sum()
        f1s.append(2 * tp / max(2 * tp + fp + fn, 1))
    out["macroF1"] = float(np.mean(f1s))
    out["balancedAccuracy"] = float(np.mean([(p[t == k] == k).mean() for k in np.unique(t)]))
    if prob is not None:
        c = prob[m] >= threshold
        out["coverage"] = float(c.mean())
        out["accuracyCovered"] = float((p[c] == t[c]).mean()) if c.any() else 0.0
    return out


def per_kind(pred, true):
    rows = {}
    for k in np.unique(true[true >= 0]):
        sel = true == k
        rows[KINDS[k]] = {"n": int(sel.sum()), "recall": float((pred[sel] == k).mean()),
                          "precision": float((true[(pred == k) & (true >= 0)] == k).mean()) if ((pred == k) & (true >= 0)).any() else 0.0}
    return rows


def auc(score, label):
    m = ~np.isnan(label)
    s, l = score[m], label[m]
    pos, neg = s[l == 1], s[l == 0]
    if not len(pos) or not len(neg):
        return float("nan")
    order = np.argsort(np.concatenate([pos, neg]))
    ranks = np.empty(len(order))
    ranks[order] = np.arange(1, len(order) + 1)
    return float((ranks[:len(pos)].sum() - len(pos) * (len(pos) + 1) / 2) / (len(pos) * len(neg)))


def group_folds(groups, k, seed=1):
    uniq = np.unique(groups)
    rng = np.random.default_rng(seed)
    rng.shuffle(uniq)
    fold_of = {g: i % k for i, g in enumerate(uniq)}
    return np.array([fold_of[g] for g in groups])


def pitch_of(raw):
    conf = raw[:, PITCH_CONF]
    with np.errstate(divide="ignore", invalid="ignore"):
        midi = 57 + 12 * (raw[:, PITCH] / conf)  # 220 Hz is A3, MIDI 57
    return conf, midi


def c_float(v):
    """A float as a C++ literal (3 is "3.0f", not "3f")."""
    t = f"{float(v):.9g}"
    return (t if any(c in t for c in ".e") else t + ".0") + "f"


def write_inc(path, ens, quantiles, thresholds, report, word_cut=None, word_ok=None):
    literal = c_float

    def arr(name, a):
        flat = np.asarray(a, np.float32).ravel()
        body = ",".join(literal(v) for v in flat)
        return f"inline constexpr float {name}[{len(flat)}] = {{{body}}};\n"

    lines = ["// Generated by tools/sound_tagger/train.py: do not edit. The sound tagger's model (SoundTagger.h).\n",
             f"// {report}\n", "#pragma once\n", "namespace sub::intelligence::tagging::model {\n",
             f"inline constexpr int kInputs = {len(INPUTS)};\n",
             f"inline constexpr int kHidden1 = {HIDDEN[0]};\n", f"inline constexpr int kHidden2 = {HIDDEN[1]};\n",
             f"inline constexpr int kEmbedding = {EMBED};\n", f"inline constexpr int kKinds = {len(KINDS)};\n",
             f"inline constexpr int kSources = {len(SOURCES)};\n", f"inline constexpr int kQualities = {len(QUALITIES)};\n",
             f"inline constexpr int kMembers = {len(ens.nets)};\n",
             "inline constexpr bool kInputMajor = true;  // (weights as [inputs][outputs])\n",
             f"inline constexpr int kWords = {len(WORD_NAMES)};\n",
             "inline constexpr const char* kWordNames[kWords] = {" + ",".join(f'"{w}"' for w in WORD_NAMES) + "};\n",
             f"inline constexpr float kTemperature = {literal(ens.temperature)};\n",
             f"inline constexpr int kInputFeature[{len(INPUTS)}] = {{{','.join(map(str, INPUTS))}}};\n",
             arr("kLow", ens.scaler.low), arr("kHigh", ens.scaler.high), arr("kMean", ens.scaler.mean),
             arr("kStd", ens.scaler.std)]
    for name, values in thresholds.items():
        lines.append(f"inline constexpr float {name} = {literal(values)};\n")
    # Per kind (and a last row for any kind), per descriptor (DESCRIPTORS' order): its 20th and 80th percentiles.
    lines.append(f"inline constexpr int kDescriptors = {quantiles.shape[1]};\n")
    lines.append(arr("kQuantiles", quantiles))
    # Per kind (and any kind last), per word: the word's score above which a sound of the kind is called it
    # (its 60th percentile among the kind's sounds); and whether the word was learned well enough to be used.
    lines.append(arr("kWordCut", word_cut))
    lines.append("inline constexpr bool kWordLearned[kWords] = {" + ",".join("true" if b else "false" for b in word_ok) + "};\n")
    for m, net in enumerate(ens.nets):
        t = net.trunk
        # Weights input-major ([inputs][outputs], PyTorch's transposed): each layer is then a sum of the outputs'
        # rows scaled by each input, which compilers vectorize, skipping the inputs ReLU made 0.
        for n, (layer, nm) in enumerate([(t[0], "W1"), (t[3], "W2"), (t[6], "W3")]):
            lines.append(arr(f"k{nm}_{m}", layer.weight.detach().numpy().T))
            lines.append(arr(f"kB{n + 1}_{m}", layer.bias.detach().numpy()))
        for head, nm in [(net.kind, "Kind"), (net.loop, "Loop"), (net.source, "Source"), (net.quality, "Quality"),
                         (net.words, "Words")]:
            lines.append(arr(f"kW{nm}_{m}", head.weight.detach().numpy().T))
            lines.append(arr(f"kB{nm}_{m}", head.bias.detach().numpy()))
    # Each layer's arrays, a member each: what the C++ loops over.
    for nm in ["W1", "B1", "W2", "B2", "W3", "B3", "WKind", "BKind", "WLoop", "BLoop", "WSource", "BSource",
               "WQuality", "BQuality", "WWords", "BWords"]:
        refs = ",".join(f"k{nm}_{m}" for m in range(len(ens.nets)))
        lines.append(f"inline constexpr const float* k{nm}[kMembers] = {{{refs}}};\n")
    digest = hashlib.sha256("".join(lines).encode()).hexdigest()[:16]
    lines.insert(4, f'inline constexpr const char* kModelId = "{digest}";\n')
    lines.append("}  // namespace sub::intelligence::tagging::model\n")
    Path(path).write_text("".join(lines))


def write_reference(path, ref):
    """A few sounds' descriptors and what the shipped model makes of them, for the C++ tests: the compiled-in
    model has to give the same (tests/intelligence/test_sound_tagging.cpp)."""
    def floats(rows):
        return "{" + ",\n ".join("{" + ",".join(c_float(v) for v in r) + "}" for r in rows) + "}"
    lines = ["// Generated by tools/sound_tagger/train.py (--test-reference): do not edit. Sounds' descriptors and\n",
             "// what the model shipped in intelligence/src/tagging/TaggerModel.inc made of them, in PyTorch.\n",
             "#pragma once\n",
             f"inline constexpr int kReferenceSounds = {len(ref['features'])};\n",
             f"inline constexpr float kReferenceFeatures[][{len(ref['features'][0])}] = {floats(ref['features'])};\n",
             f"inline constexpr float kReferenceKinds[][{len(ref['kind'][0])}] = {floats(ref['kind'])};\n",
             f"inline constexpr float kReferenceLoop[] = {floats([ref['loop']])[1:-1]};\n",
             f"inline constexpr float kReferenceEmbedding[][{len(ref['embedding'][0])}] = {floats(ref['embedding'])};\n",
             f"inline constexpr float kReferenceWords[][{len(ref['words'][0])}] = {floats(ref['words'])};\n"]
    Path(path).write_text("".join(lines))


# The descriptors a sound's adjectives are read from, relative to its kind (feature:: in EssentiaExtractor.h).
# (pitch: the MIDI note of voiced sounds, from PitchConfidence and Pitch.) A row per kind, and one for any kind.
DESCRIPTORS = {"brightness": 24, "attack": 33, "duration": 34, "subBass": 29, "pitch": None}


def descriptor(raw, d):
    if d is not None:
        return raw[:, d]
    conf, midi = pitch_of(raw)
    return np.where(conf > 0.5, midi, np.nan)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", required=True, type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--report", type=Path)
    ap.add_argument("--test-reference", type=Path, help="where to write the C++ tests' reference outputs")
    ap.add_argument("--folds", type=int, default=5)
    ap.add_argument("--members", type=int, default=MEMBERS)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--cv-members", type=int, default=1, help="members per cross-validation fold (estimates only)")
    ap.add_argument("--quick", action="store_true", help="one member everywhere")
    ap.add_argument("--skip-cv", action="store_true", help="no cross-validation (its temperature and threshold: 1, 0.5)")
    ap.add_argument("--skip-final", action="store_true", help="no model to ship: measure only")
    ap.add_argument("--temperature", type=float, default=1.0, help="with --skip-cv: the temperature to use")
    ap.add_argument("--threshold", type=float, default=0.5, help="with --skip-cv: the kinds' threshold to use")
    args = ap.parse_args()
    torch.set_num_threads(4)
    labels = json.load(open(args.work / "labels.json"))
    feats = {}
    for f in sorted((args.work / "feat").glob("*.bin")):
        aug = "_band" in f.stem
        for p, v in read_features(f).items():
            if p in labels:  # (files the corpora leave out now, as a licence, aren't trained on)
                feats[(p, aug)] = v
    train_rows = [((p, aug), v) for (p, aug), v in feats.items() if labels[p]["corpus"] != "dirt"]
    dirt_rows = [(p, v) for (p, aug), v in feats.items() if labels[p]["corpus"] == "dirt" and not aug]
    tl = {k: labels[k[0]] for k, _ in train_rows}
    train = Data([(k, v) for k, v in train_rows], tl)
    train.aug = np.array([k[1] for k, _ in train_rows])
    dirt = Data(dirt_rows, labels)
    report = {"train": {c: int((train.corpus == c).sum()) for c in np.unique(train.corpus)}, "dirt": len(dirt)}
    print("rows", report, file=sys.stderr)

    # Dirt-Samples' near-duplicates among the training sounds (the same drum machines' samples): left out.
    sc = Scaler().fit(train.raw)
    a, b = sc(train.raw), sc(dirt.raw)
    dup = np.zeros(len(train), bool)
    for s in range(0, len(a), 2048):
        d2 = ((a[s:s + 2048, None, :] - b[None, :, :]) ** 2).mean(axis=2)
        dup[s:s + 2048] = d2.min(axis=1) < 0.02
    report["dirtDuplicatesLeftOut"] = int(dup.sum())
    print("near-duplicates of Dirt-Samples left out:", dup.sum(), file=sys.stderr)

    # --- Grouped cross-validation (whole machines and instruments held out) ---
    members = 1 if args.quick else args.members
    cv_members = 1 if args.quick else args.cv_members
    folds = group_folds(train.group, args.folds)
    oof_logits = np.zeros((len(train), len(KINDS)), np.float32)
    oof_loop = np.zeros(len(train), np.float32)
    oof_quality = np.zeros((len(train), len(QUALITIES)), np.float32)
    oof_source = np.zeros((len(train), len(SOURCES)), np.float32)
    oof_words = np.zeros((len(train), len(WORD_NAMES)), np.float32)
    for k in range(0 if args.skip_cv else args.folds):
        tr = np.where(folds != k)[0]
        te = np.where(folds == k)[0]
        ens = Ensemble(train.take(tr), cv_members, seed=k, epochs=args.epochs)
        o = ens.outputs(train.raw[te])
        oof_logits[te], oof_loop[te], oof_quality[te], oof_source[te] = o["kind_logits"], o["loop"], o["quality"], o["source"]
        oof_words[te] = o["words"]
        print("fold", k, file=sys.stderr)
    temperature = 1.0 if args.skip_cv else fit_temperature(oof_logits[~train.aug], train.kind[~train.aug])
    z = oof_logits / temperature
    prob = np.exp(z - z.max(1, keepdims=True))
    prob /= prob.sum(1, keepdims=True)
    pred, conf = prob.argmax(1), prob.max(1)
    real = ~train.aug
    # The confidence below which no kind is said: where held-out predictions are right 90% of the time.
    order = np.argsort(-conf[real & (train.kind >= 0)])
    right = (pred == train.kind)[real & (train.kind >= 0)][order]
    running = np.cumsum(right) / np.arange(1, len(right) + 1)
    cs = conf[real & (train.kind >= 0)][order]
    ok = np.where(running >= 0.90)[0]
    kind_threshold = float(max(0.3, cs[ok[-1]])) if len(ok) else 0.5
    if args.skip_cv:
        temperature, kind_threshold = args.temperature, args.threshold
    cv = {"temperature": temperature, "kindThreshold": kind_threshold}
    for name, sel in [("drums (tdm, by machine)", real & (train.corpus == "tdm")),
                      ("tonal (NSynth, by instrument)", real & (train.corpus == "nsynth")),
                      ("tonal (TinySOL, by instrument)", real & (train.corpus == "tinysol")),
                      ("all", real)]:
        cv[name] = metrics(np.where(sel, pred, -2), np.where(sel, train.kind, -1), conf, kind_threshold)
    cv["drums per kind"] = per_kind(pred[real & (train.corpus == "tdm")], train.kind[real & (train.corpus == "tdm")])
    cv["tonal per kind"] = per_kind(pred[real & (train.corpus == "nsynth")], train.kind[real & (train.corpus == "nsynth")])
    lm = real & ~np.isnan(train.loop)
    cv["loopAUC"] = auc(oof_loop[lm], train.loop[lm])
    cv["loopAccuracy@0.5"] = float(((oof_loop[lm] > 0.5) == (train.loop[lm] == 1)).mean())
    cv["qualityAUC"] = {q: auc(oof_quality[real, i], train.quality[real, i]) for i, q in enumerate(QUALITIES)}
    sm = real & (train.source >= 0) & (train.corpus == "nsynth")
    cv["sourceAccuracy (NSynth)"] = float((oof_source[sm].argmax(1) == train.source[sm]).mean())
    fm = real & (train.corpus == "fsops") & (train.source >= 0)
    cv["acousticAUC (Freesound drums: acoustic against electronic)"] = auc(oof_source[fm, 0], (train.source[fm] == 0).astype(float))
    # The words, over Freesound's sounds (by uploader): over all of them, and among a kind's own (what "hard
    # snare" asks: the harder snares).
    wm = real & ~np.isnan(train.words[:, 0])
    cv["words"] = {}
    for j, wname in enumerate(WORD_NAMES):
        y = train.words[wm, j]
        within = []
        for k in range(len(KINDS)):
            s = train.kind[wm] == k
            if s.sum() >= 40 and 5 <= y[s].sum() <= s.sum() - 5:
                within.append((auc(oof_words[wm][s, j], y[s]), s.sum()))
        cv["words"][wname] = {"positives": int(y.sum()), "AUC": auc(oof_words[wm, j], y),
                              "AUC within kinds": float(np.average([a for a, _ in within], weights=[n for _, n in within])) if within else None}
    # The kind's own descriptors against NSynth's bright and dark, for comparison with the quality head.
    ns = real & (train.corpus == "nsynth")
    centroid = train.raw[ns, 24]
    kc = train.kind[ns]
    rel = np.zeros(ns.sum())
    for k in np.unique(kc):
        s = kc == k
        rel[s] = (centroid[s] - np.median(centroid[s])) / (np.std(centroid[s]) + 1e-6)
    cv["brightAUC (centroid within kind)"] = auc(rel, train.quality[ns, 0])
    cv["darkAUC (centroid within kind)"] = auc(-rel, train.quality[ns, 1])
    # Pitch from the fingerprint, against the notes' own.
    pm = real & (train.pitch >= 0) & (train.kind >= 0)
    pconf, pmidi = pitch_of(train.raw[pm])
    voiced = pconf > 0.5
    err = np.abs(pmidi[voiced] - train.pitch[pm][voiced])
    cv["pitch"] = {"voiced": float(voiced.mean()), "within a semitone": float((err < 0.5).mean()),
                   "pitch class right": float((np.round(np.mod(pmidi[voiced] - train.pitch[pm][voiced] + 6, 12) - 6) == 0).mean())}
    if not args.skip_cv:
        report["crossValidation"] = cv
        print(json.dumps(cv, indent=1), file=sys.stderr)

    # --- Dirt-Samples, never trained on ---
    keep = np.where(~dup)[0]
    ens = Ensemble(train.take(keep), members, seed=11, epochs=args.epochs)
    ens.temperature = temperature
    o = ens.outputs(dirt.raw)
    p = ens.kind_probs(o["kind_logits"])
    dp, dc = p.argmax(1), p.max(1)
    held = {"kinds": metrics(dp, dirt.kind, dc, kind_threshold), "per kind": per_kind(dp, dirt.kind)}
    drums = np.isin(dirt.kind, [KINDS.index(k) for k in DRUM_KINDS])
    held["drums"] = metrics(np.where(drums, dp, -2), np.where(drums, dirt.kind, -1), dc, kind_threshold)
    held["tonal"] = metrics(np.where(~drums, dp, -2), np.where(~drums, dirt.kind, -1), dc, kind_threshold)
    lm = ~np.isnan(dirt.loop)
    held["loopAUC"] = auc(o["loop"][lm], dirt.loop[lm])
    held["loopAccuracy@0.5"] = float(((o["loop"][lm] > 0.5) == (dirt.loop[lm] == 1)).mean())
    unl = (dirt.kind < 0) & ~(dirt.loop == 1)
    held["unlabelled folders given a kind"] = float((dc[unl] >= kind_threshold).mean())
    conf_m = np.zeros((len(KINDS), len(KINDS)), int)
    for t, q in zip(dirt.kind[dirt.kind >= 0], dp[dirt.kind >= 0]):
        conf_m[t, q] += 1
    held["confusions"] = sorted([(int(conf_m[i, j]), KINDS[i], KINDS[j]) for i in range(len(KINDS))
                                 for j in range(len(KINDS)) if i != j and conf_m[i, j] >= 3], reverse=True)[:15]
    report["dirtSamples"] = held
    print(json.dumps(held, indent=1), file=sys.stderr)
    np.save(args.work / "dirt_embedding.npy", o["embedding"])
    json.dump(dirt.paths, open(args.work / "dirt_paths.json", "w"))

    # --- The model shipped: trained on everything ---
    if args.skip_final:
        if args.report:
            json.dump(report, open(args.report, "w"), indent=1)
        return
    final = Ensemble(train, members, seed=23, epochs=args.epochs)
    final.temperature = temperature
    # Each kind's 20th and 80th percentiles of the descriptors its adjectives are read from (full-band sounds).
    q = np.zeros((len(KINDS) + 1, len(DESCRIPTORS), 2), np.float32)
    for k in range(len(KINDS) + 1):
        s = ((train.kind == k) if k < len(KINDS) else (train.kind >= 0)) & real
        for j, d in enumerate(DESCRIPTORS.values()):
            v = descriptor(train.raw[s], d)
            v = v[np.isfinite(v)]
            q[k, j] = np.percentile(v, [20, 80]) if len(v) >= 5 else q[len(KINDS), j] if k < len(KINDS) else [0, 1]
    # (any kind's row is filled last: kinds without enough of a descriptor take it)
    for k in range(len(KINDS)):
        for j, d in enumerate(DESCRIPTORS.values()):
            if q[k, j, 0] == 0 and q[k, j, 1] == 0:
                q[k, j] = q[len(KINDS), j]
    names = KINDS + ["any"]
    report["quantiles"] = {names[k]: {n: q[k, j].tolist() for j, n in enumerate(DESCRIPTORS)} for k in range(len(names))}
    if args.out:
        summary = (f"Grouped CV kinds accuracy {cv['all'].get('accuracy', 0):.3f}; Dirt-Samples (never trained on) "
                   f"accuracy {held['kinds'].get('accuracy', 0):.3f}, macro F1 {held['kinds'].get('macroF1', 0):.3f}")
        allo = final.outputs(train.raw)
        cut = np.zeros((len(KINDS) + 1, len(WORD_NAMES)), np.float32)
        for k in range(len(KINDS) + 1):
            s = ((train.kind == k) if k < len(KINDS) else np.ones(len(train), bool)) & real
            cut[k] = np.percentile(allo["words"][s], 60, axis=0) if s.sum() >= 20 else 0
        cut[:len(KINDS)][cut[:len(KINDS)].sum(1) == 0] = cut[len(KINDS)]
        ok = [bool(v["AUC"] >= 0.7 and v["positives"] >= 20) for v in cv["words"].values()]
        write_inc(args.out, final, q, {"kKindThreshold": kind_threshold}, summary, cut, ok)
        # Reference outputs for the C++ tests: the first few training rows through the shipped model.
        o = final.outputs(train.raw[:8])
        ref = {"features": train.raw[:8].tolist(), "kind": final.kind_probs(o["kind_logits"]).tolist(),
               "loop": o["loop"].tolist(), "embedding": o["embedding"].tolist(), "quality": o["quality"].tolist(),
               "source": o["source"].tolist(), "words": o["words"].tolist()}
        json.dump(ref, open(args.work / "reference_outputs.json", "w"))
        if args.test_reference:
            write_reference(args.test_reference, ref)
    if args.report:
        json.dump(report, open(args.report, "w"), indent=1)


if __name__ == "__main__":
    main()
