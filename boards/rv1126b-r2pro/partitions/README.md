# Partition ownership

The product image owns the complete partition layout. The service requires:

- a writable persistent filesystem mounted at `/userdata`
- writable `/run`
- writable `/var/log`
- sufficient space under `/userdata/botstation` for credentials and settings

The service does not define OTA, factory-data or recovery partitions. Those
values must come from the R2 Pro board SDK once the hardware layout is confirmed.
