# SpeedBreaker on iPhone and iPad

SpeedBreaker isn't on the App Store. There are two ways to get it on your iPhone or iPad:

- **Sideload the IPA** (experimental, from v0.1.2): a sideloading tool signs it with your own
  Apple Account and installs it. Read [the memory caveat](#the-memory-caveat) first: with a free
  Apple Account it may not start, depending on the tool.
- **Build it yourself** on a Mac with Xcode, signed with your own Apple developer account: see
  [Building it yourself](#building-it-yourself). This is how it was developed and tested.

## What every device needs

- **iOS or iPadOS 17 or later** and an **A15-class chip or newer**: iPhone 13 and later, iPads
  with an M-series chip. It was tested on an iPhone 15 Pro Max, an iPhone 18 Pro Max and an iPad
  Pro 12.9-inch (M2), on version 27. Devices with 6 GB of memory or more are recommended; 4 GB
  ones (iPhone 13 and 13 mini, iPhone SE 3rd generation, iPad mini 6th generation) are untested
  and likely too small.
- **A controller, strongly recommended**: an Xbox or PlayStation controller over Bluetooth, or a
  Backbone or similar. There are no touch controls for driving (a keyboard works too). A
  three-finger tap opens the settings.
- **Your own disc image** of the USA Xbox 360 disc (an `.iso`, about 7.8 GB), and room for it plus
  the installed game (about 7.1 GB) while it installs. You can delete the image afterwards.

## Sideloading the IPA (experimental)

The IPA, `SpeedBreaker-<version>-ios.ipa`, is signed by nobody: it carries only the entitlements
the game asks for, and the tool you sideload it with signs it with your Apple Account. Check the
download against its `.sha256` file (`shasum -a 256 -c SpeedBreaker-<version>-ios.ipa.sha256`).

### The memory caveat

SpeedBreaker lays out the Xbox 360's memory in one block of 4 GB of address space, needs about 3 GB
more address space besides, and at its busiest uses about 2.5 GB of memory ([Memory](#memory)).
iOS doesn't give an app that much by default, so the app asks for two entitlements:

- **Extended Virtual Addressing** lifts iOS's address-space limit. Only a paid Apple Developer
  Program account (or an enterprise one) can sign it: Apple's
  [capability table](https://developer.apple.com/help/account/reference/supported-capabilities-ios)
  lists it for those memberships and not for a free Apple Account, Xcode refuses it for a free
  Personal Team, and the sideloading tools drop it for free accounts.
- **Increased Memory Limit** raises the memory limit, from about 3.3 GB to about 6 GB on an 8 GB
  iPhone (about 2.9 GB without it on a 6 GB one). On iOS 16 and later, on devices that support
  it, it also lifts the address-space limit as Extended Virtual Addressing does
  ([xnu, kern_exec.c](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_exec.c)).
  Apple gives it to free Apple Accounts too, but not every tool asks for it.

What that means for each way of signing (as of October 2026). The IPA itself hasn't been installed on a
device yet: the tested iPhones and iPad ran the same app built and signed with Xcode and a paid account.

| Signed with | Entitlements the app gets | Expected |
|---|---|---|
| A paid Apple Developer account, Xcode (building it yourself) | both | works, as tested |
| A paid Apple Developer account, a sideloading tool | depends on the tool (AltStore keeps only Increased Memory Limit) | likely works; untested |
| A free Apple Account, [AltStore Classic](https://altstore.io) 2.2 or later | Increased Memory Limit | likely works on devices that support it |
| A free Apple Account, Xcode Personal Team (building it yourself) | Increased Memory Limit, after removing the other one | likely works on devices that support it |
| A free Apple Account, [SideStore](https://sidestore.io) 0.7 | neither ([SideStore #1616](https://github.com/SideStore/SideStore/issues/1616)) | may start; iOS may close it in busy scenes |
| A free Apple Account, [Sideloadly](https://sideloadly.io) | unknown | unknown until tested |

Without either entitlement iOS gives an app 17 GB of address space, about 7 GB of it usable: the
4 GB can still fit on a device with more than 3 GB of memory (about 5 GB is free in one piece above
the system's shared region, and the game reserves its 4 GB first thing), but everything else then
has to fit in the 3 GB or so left, and the game in iOS's default memory limit. When the 4 GB
doesn't fit, the game says so instead of closing: a page titled **This copy of SpeedBreaker can't
start**, whose Details line names the entitlements this copy was signed with. When it starts with
iOS's default address space, or a memory limit below about 3 GB, a notice says iOS may close the
game in busy scenes.

Coming from a build you made yourself, the IPA installs as a separate app (the tools give it
another bundle identifier), with its own storage: install the game again from your disc image.
Saves made in the old app stay with it.

### Free Apple Account limits

These are Apple's, whatever the tool ([AltStore's FAQ](https://faq.altstore.io/altstore-classic/your-altstore),
[error codes](https://faq.altstore.io/altstore-world/error-codes)):

- **7 days.** An app signed by a free account stops opening after 7 days. AltStore and SideStore
  refresh it in the background or when you tap Refresh; with Sideloadly you sideload it again.
  Your installed game, saves and settings stay as long as the app keeps its bundle identifier
  (the same tool and the same Apple Account).
- **3 apps.** At most 3 sideloaded apps can be active on a device at once (AltStore and SideStore
  count themselves), and an account can register at most 10 App IDs in 7 days.
- **Developer Mode** must be on: Settings > Privacy & Security > Developer Mode (the device
  restarts).

A paid account signs for a year, without the 3-app limit.

### Steps

1. Download the IPA and its `.sha256` from the release page, and check it.
2. Sideload it with your tool of choice, signing in to it with your Apple Account:
   - **AltStore Classic** (AltServer on a Mac or PC, then AltStore on the device): My Apps, +, and
     pick the IPA.
   - **SideStore**: +, and pick the IPA.
   - **Sideloadly** (Mac or PC, with a cable): drag the IPA in, choose the device, enter your Apple
     Account and press Start.
3. On the device, trust the developer: Settings > General > VPN & Device Management, your Apple
   Account, Trust. Turn on Developer Mode if iOS asks.
4. Put your disc image in **Files > On My iPhone (or iPad) > SpeedBreaker**: AirDrop it to Files,
   drag it into the device's Files tab in Finder over a cable, or download it from iCloud Drive
   first (the installer can't read a file still in the cloud).
5. Start SpeedBreaker: the installer finds the image, checks it against the disc and installs the
   game. See [iPhone and iPad](../../README.md#iphone-and-ipad) in the main README for the rest.

If it doesn't work: the page's Details line, or Settings > Advanced > Save Bug Report (a zip in
the Files app's SpeedBreaker folder) after iOS closed the game, says what happened; the log's
`[memory]` lines give the entitlements, the address space and the memory limit, and once a minute
how close the game came to it. iOS's own record of closing an app for memory is in Settings >
Privacy & Security > Analytics & Improvements > Analytics Data, as a JetsamEvent report.

### Making the IPA

```sh
export IOS_DEPS=~/speedbreaker-ios-deps        # step 2 of Building it yourself
scripts/ios/make_ipa.sh
```

This builds the app without signing it (`IOS_UNSIGNED=1 scripts/ios/build.sh`: no team, no
Apple Account, no provisioning profile) and packages `build/ios/ipa/SpeedBreaker-<version>-ios.ipa`
(`Payload/SpeedBreaker.app`, zipped), its `.sha256`, and the entitlements beside it. The version
is `project()`'s in the top-level CMakeLists.txt. `--app <SpeedBreaker.app>` packages a Release
build you already have instead, signed or not: any signature and `embedded.mobileprovision` are
removed. The app gets a generic bundle identifier (the tools give it one of yours) and an ad-hoc
signature with no certificate or team that holds only `SpeedBreaker/SpeedBreaker.entitlements`
(or `--entitlements <plist>`'s, to test with fewer), which is where the sideloading tools read the
entitlements an app asks for. The executable's debug map, which names the folders it was built in,
is stripped; its full symbols go to `build/ios/ipa/symbols/SpeedBreaker-<version>-<commit>-ios.dSYM`,
to keep and not to publish. The license notices go in `SpeedBreaker.app/licenses` (`COPYING`,
`NOTICE`, `THIRD_PARTY_NOTICES.md` and `LICENSES/`), as in the other downloads. Before writing the IPA it checks that no home-folder path, team,
provisioning profile or device ID is left in it; `IPA_SCAN_EXTRA="A1B2C3D4E5|your name"` adds
strings of your own to look for. The same app always gives the same IPA.

## SpeedBreaker Carbon (NFS: Carbon)

The same scripts build NFS: Carbon's app with `SB_GAME=carbon`: **SpeedBreaker Carbon**, with its
own icon, version and bundle identifier, so it installs beside SpeedBreaker (Most Wanted) with its
own storage. It needs `ppc-carbon/` (XenonRecomp with `config/carbon/carbon.toml`, from the USA
Collector's Edition disc) and builds in `build/ios-carbon`:

```sh
SB_GAME=carbon IOS_BUNDLE_ID=com.yourname.SpeedBreakerCarbon scripts/ios/build.sh install
SB_GAME=carbon scripts/ios/make_ipa.sh     # SpeedBreaker-Carbon-<version>-ios.ipa
```

Its folder in the Files app is **On My iPhone (or iPad) > SpeedBreaker Carbon**: put the Collector's
Edition disc image there. Everything else on this page applies to it as it is.

## Memory

What the game needs, measured on a Mac (macOS 27, Apple silicon) in a scripted run into the first
race, with the same code as the iOS app:

- **Address space:** one block of 4 GB for the guest (no smaller layout exists: the recompiled code
  addresses guest memory as one base plus a 32-bit address, and the game's memory runs to
  0xFFFFFFFF), plus about 3 GB for everything else at 1x internal resolution, the iPhone's
  default (the graphics driver's memory, the game's heaps, thread stacks), 3.8 GB at 2x2.
- **Memory footprint, peak:** 2.2 GB at 1x, 2.8 GB at 2x2 (1440p). The graphics memory in that
  race was 1.18 GB at 1x; on an iPhone 15 Pro Max in free roam it was 1.46 GB, so expect about
  2.5 GB there.
- On iOS the log's `[memory]` lines give the real figures: the address space's top (0xfc0000000
  or more with an entitlement, 0x458000000 without), the memory limit, and once a minute the
  footprint, its peak and what is left.

## Building it yourself

### What you need

- **A Mac** with Apple silicon and **Xcode** (built with Xcode 26.4 and 27.0), plus everything the
  Mac build needs: see [Building from source](../../README.md#building-from-source), step 1.
- **Your own disc image**, for step 2 of that guide (the game's code is translated from it on your
  Mac) and later for the installer on the device.
- **An Apple developer account.** SpeedBreaker was built and tested with a paid Apple Developer
  Program membership. A free Apple Account's Personal Team can't sign Extended Virtual Addressing:
  Xcode stops with "Personal development teams ... do not support the Extended Virtual Addressing
  capability". Delete that key (and its `<true/>`) from `scripts/ios/SpeedBreaker/SpeedBreaker.entitlements`
  first and keep Increased Memory Limit (see [the memory caveat](#the-memory-caveat)); apps signed
  by a free account also stop opening after 7 days.

### 1. The game's code

On the Mac, follow [Building from source](../../README.md#building-from-source) steps 1 and 2:
`scripts/setup_tools.sh`, your disc's `default.xex` in `game/files`, and XenonRecomp, which
writes `ppc/`. You don't need the Mac build itself (step 3).

### 2. The iOS libraries

```sh
scripts/ios/build_deps.sh ~/speedbreaker-ios-deps
```

This downloads and builds SDL3, glslang and FFmpeg's XMA decoders for iOS, and unpacks MoltenVK's
static library, into the folder you name (a few minutes; the sources, about 65 MB, are checked
against their SHA-256). Nothing is installed anywhere else.

### 3. Signing

Find your **team ID**: in Xcode, Settings > Accounts, select your account and team, or on
developer.apple.com under Membership details. It's 10 characters, like `A1B2C3D4E5`. Pick a
**bundle identifier** of your own, such as `com.yourname.SpeedBreaker`: it must be unique to your
team.

On the device, turn on **Developer Mode** (Settings > Privacy & Security > Developer Mode; it
restarts), then connect it to the Mac with a cable and tap **Trust**.

### 4. Build and install

```sh
export IOS_DEPS=~/speedbreaker-ios-deps
export DEVELOPMENT_TEAM=A1B2C3D4E5            # your team ID
export IOS_BUNDLE_ID=com.yourname.SpeedBreaker
scripts/ios/build.sh install
```

`build.sh` compiles the game into `build/ios` (about 6 minutes the first time), links and signs the
app with Xcode's automatic signing, which makes the provisioning profile itself, and installs it on
the connected device. The device must be registered with your team: if signing fails because the
team has no devices, add it on developer.apple.com under Devices, with the UDID Finder shows for it
(select the device in Finder's sidebar, then click the text under its name). Without `install` it
only builds: `build/ios/xcode/Build/Products/Release-iphoneos/SpeedBreaker.app`. With more than one
device connected, set `IOS_DEVICE` to the one you want (`xcrun devicectl list devices`).

If the app doesn't open and iOS says the developer isn't trusted, go to Settings > General > VPN &
Device Management, choose your developer account and trust it.

### 5. Play

Copy your `.iso` into **Files > On My iPhone (or iPad) > SpeedBreaker** and start the app: the
installer finds it, checks it and installs the game. See
[iPhone and iPad](../../README.md#iphone-and-ipad) in the main README for the rest: controllers,
the settings (a three-finger tap) and frame rates.

To update, pull the new version, repeat step 1's XenonRecomp line if `config/` changed, and run
`scripts/ios/build.sh install` again: your installed game and saves stay.
