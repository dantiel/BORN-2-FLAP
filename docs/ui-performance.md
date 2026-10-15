# Ruby / UMGHAML UI performance — 2026-10-15

The UI remains authored through UMGHAML/UMGSS and the Ruby Brain. These changes fix transport and reconciliation overhead rather than switching back to native screen layouts.

## Causes and changes

- The game previously wrote the complete telemetry file every simulation frame. Snapshot production and frame polling now run at at most 20 Hz; a button queues an immediate next-tick update.
- Replacing telemetry through the default Unreal file manager could retry a Windows sharing violation with a 500 ms sleep on the game thread. Replacement now uses `bDoNotRetryOrError`; a failed publication is retried on the next UI tick. Only successful publications update the cached snapshot.
- Identical JSON is not rewritten. Ruby skips identical input, avoids reauthoring an unchanged visual state, and still acknowledges queued input commands.
- F7 publishes a compact hidden state and omits live flight/settings/catalog telemetry. Dashboard collapses its root and removes all panel children. Restoring F7 publishes the current full state.
- Reconciler sibling removals now run in descending index order. Removing them in ascending order shifted later indices and could leave panels mounted.
- File rotation reuses the existing Transport event subscriptions; it no longer adds listeners retaining closed output streams.
- Flight readouts are rounded to their displayed precision before reconciliation, avoiding widget invalidation for invisible floating-point changes.

## Verification

- Editor Development build: succeeded.
- Ruby suite: 98 tests, 492 assertions, no failures.
- `-B2FRubyUIPerfTest` on Training: six-second settled hidden window recorded 0 telemetry writes, 0 applied UI frames, maximum bridge tick 0.359 ms. Restoration produced updates again. Both rendered screenshots were inspected.
- Runtime log: `Unreal/Born2Flap/Saved/Logs/ruby-ui-perf.log`.
- Captures: `Unreal/Born2Flap/Saved/Screenshots/WindowsEditor/RUBY_UI_hidden.png` and `RUBY_UI_restored.png`.

Run the focused engine check with:

```powershell
& 'V:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'V:/BORN 2 FLAP/Unreal/Born2Flap/Born2Flap.uproject' /Engine/Maps/Entry -game -unattended -nosplash -windowed -ResX=1440 -ResY=900 -B2FLevel=Training -B2FNoMenu -B2FRubyUIPerfTest
```

This measures the bridge, not total world/render frame time. It does not establish full UI feature parity or Shipping-package performance. Hidden mode retains lightweight polling so UI restoration and commands remain responsive.
