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
