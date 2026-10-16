# ZMK Smart Mouse Layer

ZMK input processor and behavior for trackball keyboards that switch to a mouse layer
automatically.

- Activates the mouse layer only after pointer movement exceeds a threshold, and not right
  after typing.
- Lets you type right after a click: a key on a mouse-button position clicks only while you
  are evidently pointing, otherwise the layer exits and the key types its base-layer letter.
- Provides `&sml` keys to lock the layer (toggle or hold) or exit it explicitly.
- Supports an explicit-exit mode and a manual-only mode (no automatic activation).

Tested with ZMK v0.3.

## Installation

Add the module to `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: archfill
      url-base: https://github.com/archfill
  projects:
    - name: zmk-smart-mouse-layer
      remote: archfill
      revision: main
```

## Basic setup

Define the input processor and attach it to the pointing device's input listener:

```dts
#include <dt-bindings/zmk/keys.h>

/ {
    /omit-if-no-ref/ smart_mouse_layer: smart_mouse_layer {
        compatible = "zmk,input-processor-smart-mouse-layer";
        #input-processor-cells = <2>;
        movement-threshold = <8>;
        require-prior-idle-ms = <300>;
        keep-behaviors = <&mkp &mmv &msc>;
        keep-keycodes = <LCTRL RCTRL LSHFT RSHFT LALT RALT LGUI RGUI>;
        transparent-behaviors = <&trans>;
        click-window-after-move-ms = <600>;
        click-window-after-click-ms = <400>;
        warm-reactivation-ms = <1000>;
        warm-movement-threshold = <2>;
        keep-layer-bindings;
    };
};

&trackball_central_listener {
    input-processors = <&smart_mouse_layer 8 2000>;
};
```

The two input processor cells are:

1. target (mouse) layer index
2. inactivity timeout in milliseconds

## How it works (default `exit-mode = "auto"`)

**Activation.** Accumulated `INPUT_REL_X`/`INPUT_REL_Y` movement must reach
`movement-threshold`, and at least `require-prior-idle-ms` must have passed since the last key
press.

**Key presses while the layer is active.** The processor resolves the effective binding of the
pressed position (skipping `transparent-behaviors`) and classifies it:

| Class | Binding | Result |
|---|---|---|
| Button | in `keep-behaviors` (e.g. `&mkp`) | click (see click / type decision) |
| Layer key | other non-transparent binding on the mouse layer or above, with `keep-layer-bindings` | keeps the layer (macros, `&mo` to a scroll layer, ...) |
| Modifier | a binding parameter matches `keep-keycodes` | keeps the layer (shift-click, ctrl-click) |
| Typing | anything else (transparent on the mouse layer) | exits the layer; the key types its base-layer letter |
| `&sml` | this module's behavior | ignored by the processor |

While a button, layer key or modifier is held the inactivity timeout is suspended. Otherwise the
layer exits after the timeout, measured from the last pointer movement or key release.

**Click / type decision.** When at least one click window is nonzero, a button press clicks only
if one of these holds; otherwise the layer exits and the key types:

- the last pointer movement was within `click-window-after-move-ms`
- the last button release was within `click-window-after-click-ms` (any button: double click,
  left then right click in place)
- another button / layer key / modifier is held (chord, drag)

Releasing a button consumes the movement window, so after a click only the click window or new
pointer movement allows another click. Decisions are logged at debug level with the elapsed
times, which helps tuning the windows.

The order matters: the processor's `position_state_changed` listener runs before hold-tap,
combos and the keymap, so deactivating the layer there makes the keymap resolve the key on the
layers below.

**Warm re-activation.** After the layer exits by timeout, for `warm-reactivation-ms` the
activation threshold is `warm-movement-threshold`, so a tiny nudge of the ball brings the layer
back. `require-prior-idle-ms` still applies.

## Locks and explicit exit: the `&sml` behavior

```dts
#include <dt-bindings/zmk/smart_mouse_layer.h>

/ {
    sml: smart_mouse_layer_behavior {
        compatible = "zmk,behavior-smart-mouse-layer";
        #binding-cells = <1>;
        input-processor = <&smart_mouse_layer>;
        layer = <8>;
    };
};
```

| Binding | Action |
|---|---|
| `&sml SML_TOG` | toggle lock: activate and lock the layer, press again to exit |
| `&sml SML_HOLD` | hold lock: layer locked while held, exits on release (unless toggle-locked) |
| `&sml SML_EXIT` | exit the layer and clear all locks |

While locked there is no inactivity timeout and button keys always click. Typing keys keep the
layer while hold-locked, and exit a toggle lock only with `toggle-exit-on-typing`.

The keys work on any layer. On the mouse layer they only work after the layer is active; on a
base layer `SML_TOG` / `SML_HOLD` activate the layer without moving the ball.

## Modes

| Goal | Settings |
|---|---|
| Automatic in and out (default) | defaults + click windows |
| Automatic in, lock when needed | add `&sml SML_TOG` and/or `&sml SML_HOLD` |
| Automatic in, explicit out | `exit-mode = "explicit"` + `&sml SML_EXIT` (or `explicit-exit-on-typing`) |
| Manual only | `disable-auto-activate` + `&sml SML_TOG` / `&sml SML_HOLD` |

With `exit-mode = "explicit"` and neither `&sml SML_EXIT` nor `explicit-exit-on-typing`, there is
no way to leave the layer except other layer changes.

## Properties

| Property | Type | Default | Description |
|---|---|---|---|
| `movement-threshold` | int | `0` | accumulated X/Y movement to activate |
| `require-prior-idle-ms` | int | `0` | idle time after a key press before activation is allowed (`300` is a good start) |
| `keep-behaviors` | phandles | required | button behaviors (`&mkp &mmv &msc`) |
| `keep-keycodes` | array | `[]` | keycodes that keep the layer (modifiers) |
| `transparent-behaviors` | phandles | required | behaviors skipped when resolving bindings (`&trans`) |
| `click-window-after-move-ms` | int | `0` | button clicks if pointer moved within this time (`0` = always click) |
| `click-window-after-click-ms` | int | `0` | button clicks if a button was released within this time |
| `warm-reactivation-ms` | int | `0` | warm period after a timeout exit |
| `warm-movement-threshold` | int | `0` | activation threshold during the warm period |
| `disable-auto-activate` | boolean | off | never activate from pointer movement |
| `exit-mode` | string | `"auto"` | `"auto"` or `"explicit"` (no timeout, buttons always click) |
| `explicit-exit-on-typing` | boolean | off | in explicit mode, typing keys exit the layer |
| `toggle-exit-on-typing` | boolean | off | while toggle-locked, typing keys exit the layer |
| `keep-layer-bindings` | boolean | off | non-button bindings on the mouse layer keep it |

With all optional properties unset the processor behaves like earlier versions: buttons always
click and the layer exits on typing keys or timeout.

## Split keyboards

The keymap is compiled for every half, so if the keymap uses `&sml`, define both the processor
node and the behavior node in a file shared by all halves (e.g. the shield's `.dtsi`). The
drivers are only built where `ZMK_POINTING` is enabled; the behavior runs on the central.

## Status

Experimental. Tested on a moNa2 (PMW3610 trackball, ZMK v0.3).

## License

MIT. See [LICENSE](LICENSE).
