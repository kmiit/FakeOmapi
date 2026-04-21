# Fake omapi service written in cpp

For running SecureElement service (omapi) without java environment.  
Providing a method to decrypt in native environment like TWRP.

Only implemented **part** of original [SecureElement](https://android.googlesource.com/platform/packages/apps/SecureElement/).

## How to use

- Clone this repo to aosp source tree, eg. $TWRP_ROOT/vendor/twrp
- lunch target device
- run `make se_omapi`

Then, the binary will be found in out/target/product/$DEVICE/vendor

- Copy binary file, needed dependencies, manifest file and rc file to your TWRP device tree
- Build twrp
- Enjoy

## License

This project is licensed under the Apache License 2.0.  
See the [LICENSE](LICENSE) file
