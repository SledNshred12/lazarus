# Lazarus (Android)

A small Android app that runs [Lazarus](https://lazarus-a2j.pages.dev/) in its own window.

**Why this exists:** it uses Android's built-in web engine, so it works on phones where
Chrome is blocked or missing. No browser app is needed.

## Get the app

Go to [Releases](../../releases/latest) and download **Lazarus.apk** on your Android phone.
Open it, and allow installing from this source when Android asks.

It needs internet the first time so the game can save itself to your phone.
After that it opens and plays with wifi and data off.

Online worlds, invites and accounts still need a connection, as you'd expect.

## Updates

You don't need a new APK when the game updates. The app loads the live game, so it
picks up new versions by itself.

## Building

Pushing to `main` builds and signs the APK automatically and puts it on the Releases page.
The signing key lives in the repository secrets (`KEYSTORE_B64`, `KEY_PASS`) so every build
is signed with the same key and installs over the last one.
