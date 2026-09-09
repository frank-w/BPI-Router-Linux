.. SPDX-License-Identifier: GPL-2.0

========================
mxl862xx devlink support
========================

This document describes the devlink features implemented by the
``mxl862xx`` device driver.

Info versions
=============

The ``mxl862xx`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Example
     - Description
   * - ``asic.id``
     - fixed
     - 8628
     - The chip part number read from the CHIP ID registers. Omitted
       when the part number reads as zero, which happens for a switch
       sitting in MCUboot rescue mode (the registers need a running
       firmware), for an unfused part, and after a failed flash.
   * - ``asic.rev``
     - fixed
     - 0
     - The chip version read from the same register word, so it is
       omitted whenever ``asic.id`` is.
   * - ``fw``
     - running, stored
     - 1.0.70
     - Version of the firmware running on the switch, reported as both
       running and stored since the switch boots it from its own flash.
       It is omitted while no firmware version is known: after a failed
       flash, and in MCUboot rescue mode while an interrupted download
       is still being recovered in the background. Once the loader is
       ready to accept a new image the version appears as "0.0.0",
       which no released firmware reports, so version-comparing tools
       offer any available release as an upgrade. Use ``devlink dev
       flash`` to tell a recovering switch from a ready one, see below;
       a missing version on its own does not say why.

Flash update
============

The ``mxl862xx`` driver implements support for ``devlink dev flash``.
The signed firmware image is transferred to the switch over the same
MDIO bus which is also used to manage the switch, then verified and
installed by the MCUboot bootloader running on the switch. All ports
of the switch are closed for the duration of the update and the driver
reprobes the switch after it has rebooted into the new firmware. A
complete flash and reprobe cycle takes about one minute.

A switch stuck in MCUboot rescue mode, e.g. after an interrupted
update, is registered without user ports. If the previous download was
interrupted mid-transfer the loader is wedged; the driver drains it
back to a clean ready state in the background, one byte at a time,
which takes tens of minutes for a large image and is reported through
the kernel log as it progresses. During that recovery ``devlink dev
flash`` returns ``-EBUSY`` with an extack message saying so, and
``devlink dev info`` reports no firmware version. Once the loader is
ready the firmware version appears and flashing a firmware image
through the regular update flow recovers the switch.

If the recovery fails, the loader needs a power cycle: ``devlink dev
flash`` then returns ``-EIO`` and says so in its extack message. The
driver only re-examines the switch when it binds, so on a board where
the switch can be power cycled on its own, unbind and rebind the driver
afterwards to have the recovered switch recognised.
