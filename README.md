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
- `transparent-behaviors`: behaviors to skip when resolving the effective binding.

## Status

This module is an initial implementation intended for real-device testing.
