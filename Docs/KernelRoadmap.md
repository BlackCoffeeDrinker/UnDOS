# Kernel roadmap

[Project overview](../README.md)

## Planned work

```mermaid
sequenceDiagram
    participant Kernel
    participant HostCtrl as HostController
    participant PciBusRoot as PciBus(root)
    participant PDO as DeviceObject(PDO)
    participant IOManager
    participant DriverStore
    participant Driver as DriverObject
    participant FDO as FDO/LDO
    participant Bridge as PciBridgeDevice
    participant PciBusChild as PciBus(child)
    Kernel ->> HostCtrl: bind PCI host controller
    HostCtrl ->> PciBusRoot: create(bus=0)
    PciBusRoot ->> HostCtrl: scan devices on bus 0

    alt normal device
        PciBusRoot ->> PDO: create PDO (fill DeviceExtension)
        PDO ->> IOManager: publish IDs and resources
        IOManager ->> DriverStore: match driver for PDO
        DriverStore ->> Driver: load driver
        IOManager ->> Driver: pass PDO to DriverObject (bind)
        Driver ->> FDO: create FDO or install callbacks
        Driver ->> PDO: attach FDO to PDO stack
        PDO ->> Driver: invokeStart (start callback)
    else PCI-to-PCI bridge
        PciBusRoot ->> Bridge: create bridge PDO
        Bridge ->> HostCtrl: read secondary/subordinate bus numbers
        Bridge ->> PciBusChild: create(bus=secondary)
        PciBusChild ->> HostCtrl: scan devices on child bus
        loop enumerate child devices
            PciBusChild ->> PDO: create PDO for child device
            PDO ->> IOManager: publish child IDs/resources
            IOManager ->> DriverStore: match driver
            DriverStore ->> Driver: load driver
            IOManager ->> Driver: bind child PDO
            Driver ->> FDO: create/attach FDO for child
            PDO ->> Driver: invokeStart
        end
    end

%% Teardown / removal flow
    note over Driver, IOManager: On driver unload or device removal
    Driver ->> FDO: detach / remove FDO
    IOManager ->> PDO: mark PDO removed
    PciBusRoot ->> PDO: remove PDO from device list
    Bridge ->> PciBusChild: destroy child bus (after child PDOs removed)
    PDO ->> Driver: invokeRemove
```

* [X] Implement initial GDT reload structure inside x86 HAL initialization sequence
* [X] Implement Object Manager
* [X] Implement scheduler
* [X] Implement VFS
* [X] Implement Driver Runtime (loading & executing)
* [ ] Complete early device tree parsing from `CONFIG.REG` file node arrays
* [ ] Make a HAL for ACPI x86
* [ ] Make a HAL for SMP devices
