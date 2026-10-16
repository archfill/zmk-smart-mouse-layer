# ZMK Smart Mouse Layer

Experimental ZMK input processor for trackball keyboards.

`zmk-smart-mouse-layer` activates a mouse layer only after pointer movement exceeds a
configured threshold. It can also keep the layer active when the effective key binding
at a pressed position resolves to a configured mouse behavior, such as `&mkp`, `&mmv`,
or `&msc`.

## Example

```dts
/ {
    /omit-if-no-ref/ smart_mouse_layer: smart_mouse_layer {
        compatible = "zmk,input-processor-smart-mouse-layer";
        #input-processor-cells = <2>;
        movement-threshold = <8>;
        require-prior-idle-ms = <300>;
        keep-behaviors = <&mkp &mmv &msc>;
        keep-keycodes = <LCTRL RCTRL LSHFT RSHFT LALT RALT LGUI RGUI>;
        transparent-behaviors = <&trans>;
    };
};

&trackball_central_listener {
    input-processors = <&smart_mouse_layer 8 2000>;
};
```

The two input processor cells are:

1. target layer index
2. inactivity timeout in milliseconds

Optional properties:

- `movement-threshold`: accumulated `INPUT_REL_X`/`INPUT_REL_Y` movement required before
  activating the layer. Set this higher to ignore more small trackball jitter.
- `require-prior-idle-ms`: idle time required after key input before pointer movement can
  activate the layer. Omit it or set it to `0` to disable this guard. `300` is a practical
  starting point; try `500` if typing still triggers the layer too often.
- `keep-behaviors`: behaviors that should keep the mouse layer active when pressed.
- `keep-keycodes`: keycodes that should keep the mouse layer active when they appear in
  either binding parameter, including modifier keycodes inside `&kp`, `&mt`, or `&lt`.
  While a matching binding is held, the inactivity timeout is suspended and resumes after
  the binding is released.
- `transparent-behaviors`: behaviors to skip when resolving the effective binding.

- `click-window-after-move-ms` (optional, default `0`): when nonzero, a press on a
  `keep-behaviors` position (e.g. `&mkp`) counts as a mouse click only if the last pointer
  movement was at most this many ms ago. Releasing a click consumes this window, so after a
  click only `click-window-after-click-ms` or new pointer movement allows another click.
- `click-window-after-click-ms` (optional, default `0`): a `keep-behaviors` press also counts
  as a click if any `keep-behaviors` position was released at most this many ms ago (double
  click, left then right click in place).
- `warm-reactivation-ms` / `warm-movement-threshold` (optional, default `0`): after the layer
  is deactivated by the inactivity timeout (not by a key press), for `warm-reactivation-ms`
  the activation threshold is `warm-movement-threshold` instead of `movement-threshold`.
  `require-prior-idle-ms` still applies.

### Click / type decision

When at least one of the two click windows is nonzero, a `keep-behaviors` press while the layer
is active is treated as a click if any of the following holds:

- `now - last_move <= click-window-after-move-ms`
- `now - last_click_release <= click-window-after-click-ms`
- another `keep-behaviors` / `keep-keycodes` position is currently held (chord, drag)

Otherwise the layer is deactivated and the event bubbles, so the key resolves on the layers
below (the base-layer letter is typed). `keep-keycodes` (modifiers) always keep the layer.
With both windows `0` the behavior is unchanged. Suggested values: `600` / `400` / `1000` / `2`.
Decisions are logged at debug level with the elapsed times.

### Exit modes, locks and the `&sml` behavior

Additional optional properties of the input processor:

- `disable-auto-activate` (boolean): pointer movement never activates the layer; use the behavior.
- `exit-mode` (`"auto"` default, or `"explicit"`): `auto` is the behavior described above.
  `explicit` has no inactivity timeout, `keep-behaviors` presses always click (no click-window
  judgment), and typing keys exit the layer only if `explicit-exit-on-typing` is set.
- `explicit-exit-on-typing` (boolean): see above.
- `toggle-exit-on-typing` (boolean): while toggle-locked, typing keys exit the layer.
- `keep-layer-bindings` (boolean): any binding found on the target layer (or a layer above it)
  that is not a `keep-behaviors` entry (macro, `&mo`, ...) keeps the layer instead of exiting it.
  `keep-behaviors` are recognized on any layer.

Behavior `zmk,behavior-smart-mouse-layer` (needs `input-processor = <&processor>` and `layer = <N>`;
`#binding-cells = <1>`), constants from `<dt-bindings/zmk/smart_mouse_layer.h>`:

- `SML_TOG`: toggle lock. Activates and locks the layer; pressing again deactivates it. No timeout
  while locked, button keys always click.
- `SML_HOLD`: hold lock. Layer active while held, typing keys never exit. On release the layer is
  deactivated unless toggle-locked.
- `SML_EXIT`: deactivates the layer and clears all lock state.

The behavior keys themselves are ignored by the processor (they never exit the layer).
The inactivity timeout only applies to an automatically entered layer in `exit-mode = "auto"`.

## Status

This module is an initial implementation intended for real-device testing.
