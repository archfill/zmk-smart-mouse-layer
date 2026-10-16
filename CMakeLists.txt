# Copyright (c) 2026 The ZMK Smart Mouse Layer Contributors
# SPDX-License-Identifier: MIT

zephyr_include_directories(include)

if(CONFIG_ZMK_SMART_MOUSE_LAYER)
  target_sources(app PRIVATE src/input_processor_smart_mouse_layer.c)
endif()

if(CONFIG_ZMK_BEHAVIOR_SMART_MOUSE_LAYER)
  target_sources(app PRIVATE src/behavior_smart_mouse_layer.c)
endif()
