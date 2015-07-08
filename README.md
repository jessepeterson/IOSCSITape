# IOSCSITape

IOSCSITape provides a typical Unix-like tape driver for Mac OS X. This allows for standard utilities like `tar` to directly read and write to tape.

Despite being a Unix-like operating system Mac OS X has never included a Unix-like tape driver. Since Mac OS X's release in 2001 Apple has instead wanted developers to use a separate [Mac OS X-proprietary API](http://developer.apple.com/library/mac/documentation/DeviceDrivers/Conceptual/WorkingWithSAM/WWS_SAMDevInt/WWS_SAM_DevInt.html) for accessing devices like tape drives. Unfortunately this forces developers to reinvent an already well-established wheel. As well to adapt to said API would limit the flexibility and platform-agnostic nature of some standard and very popular tools and methodologies of working with tape drives.

IOSCSITape provides a typical Unix-like character device file (e.g. `/dev/rst0`) with accompanying tool (`mt`) for tape drive manipulation on Mac OS X. IOSCSITape aims to bring the capability of running standard tools like `tar` and `dd` as well as popular tools like [Amanda](http://www.amanda.org/) and [Bacular](http://www.bacula.org/) to Mac OS X.

**BE ADVISED:** IOSCSITape is not yet intended for production use. It has had limited testing on a very limited set of hardware and is still in early development. Use at your own risk.

_*Can you part with modern tape drives*, autoloaders/libraries, OS X SCSI adapters, or other related equipment? The IOSCSITape project could use donations of equipment to improve and better support a range of devices. Please get in touch with jesse.c.peterson-att-gmail if so. Please: only modern equipment and serious offers._
