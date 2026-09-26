# wxl-retail-ui

Experimental WarcraftXL ABI 1.1 compatibility foundation for backported retail interface modules.

This review branch contains only the reusable UI compatibility layer. It fills APIs that are absent
from the 3.3.5a client without replacing native WotLK globals or promising arbitrary retail-addon
compatibility.

The current foundation includes compatibility contracts for UI helpers, timers, settings, frame
pools, retail-style animation proxies, and atlas sizing behavior. Ping-specific APIs remain owned by
`wxl-radial-ping`.

Weekly Rewards is deliberately not included in this repository update. Its protocol, client UI,
server ownership, and localization are still being developed and validated separately.

## Requirements

- WarcraftXL Core ABI 1.1 with the FrameScript service contract.
- A reviewed client interface payload that consumes these compatibility shims.

## Status

Experimental source only. The repository has no Hub manifest, module topic, release workflow, or
binary release yet.

## License

GPL-3.0-or-later. See `LICENSE`.

## Integration and release checks

This repository is a WIP compatibility foundation, not the complete Eunoia UI. The current integration checkout also contains native Weekly Rewards, built-in transmog/outfits/situations, localized FrameNew files, and matching server protocols; those components are not present in this repository's current tree. Do not advertise or install this repo as a standalone transmog or Weekly Rewards package. It has no Hub manifest or release workflow yet.

For source development, build the `wxl-retail-ui` Win32 target against the exact compatible WXL core and Runtime FrameScript API, then use `/wxlretailcompat` to inspect the available compatibility shims. A later release needs an explicit FrameNew/locale payload, protocol and server dependencies, package manifest, smoke test, and rollback. Keep the older client UI working until that coordinated package is validated.

## Credits

The WXL core ABI and original module interfaces come from WarcraftXL contributors. The local v1.1 integration commits in this snapshot are attributed to Furioz in the integration history. Preserve source-file notices and the GPL-3.0-or-later `LICENSE` when redistributing source or binaries.
