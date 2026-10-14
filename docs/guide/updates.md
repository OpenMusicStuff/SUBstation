# Updates

SUBstation can keep itself up to date. **Options › Preferences… › Updates** chooses which versions you're offered and
how they arrive; **Help › Check for Updates…** looks at once.

## Update channel

- **Stable** (recommended): tested releases only.
- **Nightly**: a build of the day's changes, each day there were some, and the tested releases. The newest features,
  a day old.
- **Canary**: a build of every change as it is made. The newest, and the least tested: expect things to break.

A channel takes in the ones above it: on Nightly you're offered a stable release when it's newer than your nightly.
Going back to Stable from a newer nightly offers nothing until a stable release is newer than what you have.

## Checking and installing

- **Automatically check for updates**: SUBstation looks soon after it starts and then once a day. An update found
  says so in the status line. Off, it looks only when you click **Check Now** (or use Help › Check for Updates…).
- **Install updates automatically after closing the app**: an update found downloads in the background and installs
  when you close SUBstation; the next time you start it, it's the new version. Off, you choose: **Download and
  Install**, then **Restart to Install**, which closes SUBstation (asking about unsaved changes, as Quit does),
  installs the update and starts it again.

The box under them sums up what your choices mean. Below it the page says what the last look found, which version you
have, and when it last looked; **Release Notes** opens the update's page.

Every download is checked against its checksum before it's installed; one that doesn't match is thrown away.

## Good to know

- SUBstation installs an update over the folder it runs from: keep it in a folder you can write to (your user folder,
  not *Program Files*).
- Updates install on Windows. On Linux SUBstation tells you an update is out and links its page.
- A build you made yourself never looks by itself: Check Now still works.
- Your projects, presets and preferences aren't touched by an update.
