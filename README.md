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
        require-prior-idle-ms = <1000>;
        keep-behaviors = <&mkp &mmv &msc>;
        transparent-behaviors = <&trans>;
    };
};

&trackball_central_listener {
    input-processors = <&smart_mouse_layer 8 2000>;
};

&mkp_input_listener {
    input-processors = <&smart_mouse_layer 8 2000>;
};

&mmv_input_listener {
    input-processors = <&smart_mouse_layer 8 2000>;
};

&msc_input_listener {
    input-processors = <&smart_mouse_layer 8 2000>;
};
```

The two input processor cells are:

1. target layer index
2. inactivity timeout in milliseconds

## Status

This module is an initial implementation intended for real-device testing.
