# Display service

Implemented by the adjacent `oledview.c/h` module and production `cfgbind`
worker binding. It renders copied observations from configured source queues
into the existing SSD1306 frame; no second display framework is required.
See [contracts, review and acceptance limits](../DISPLAY.md).
