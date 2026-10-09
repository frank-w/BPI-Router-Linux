.. SPDX-License-Identifier: GPL-2.0

=============
PCS Subsystem
=============

The PCS (Physical Coding Sublayer) subsystem handles the registration and lookup
of PCS devices. These devices contain the upper sublayers of the Ethernet
physical layer, generally handling framing, scrambling, and encoding tasks. PCS
devices may also include PMA (Physical Medium Attachment) components. PCS
devices transfer data between the Link-layer MAC device, and the rest of the
physical layer, typically via a serdes. The output of the serdes may be
connected more-or-less directly to the medium when using fiber-optic or
backplane connections (1000BASE-SX, 1000BASE-KX, etc). It may also communicate
with a separate PHY (such as over SGMII) which handles the connection to the
medium (such as 1000BASE-T).

Remark on usage of .mac_select_pcs and fw_node PCS
--------------------------------------------------

There are generally two ways to look up a PCS device.

1. MAC OP struct .mac_select_pcs (considered deprecated)
2. Internal PCS handling with ``num_possible_pcs`` and ``fill_available_pcs``

Implementation 1 leaves the entire handling of the PCS to the MAC
driver with the selection of the PCS driven by .mac_select_pcs.
Custom implementations are required if the PCS is external to the MAC
and needs to be handled by a separate driver.

Implementation 2 makes the phylink core code to select the PCS
provided by ``num_possible_pcs`` and ``fill_available_pcs`` either
via internal MAC handling or firmware node (fwnode)

Implementation 1 is considered deprecated and it's suggested to
switch to implementation 2 and where possible switch to firmware
node design.

.. _pcs_fwnode:

Looking up PCS Devices (fwnode implementation)
-----------------------------------------------

The lookup of a PCS device follows the common producer/consumer implementation
used by similar subsystems with a ``#pcs-cells`` on the producer and a
``pcs-handle`` property on the consumer::

    pcs: pcs {
        // ...
        #pcs-cells = <0>;
    };

    ethernet-controller {
        // ...
        pcs-handle = <&pcs>;
    };

On :c:func:`phylink_create`, phylink will use the ``num_possible_pcs``
value and ``fill_available_pcs`` helper function in
:c:struct:`phylink_config` to compose the list of available PCS that can be
used for the phylink instance.

Phylink will then internally handle the selection of the correct PCS for
the requested interface mode based on the interface modes configured in
``pcs_interfaces`` in :c:struct:`phylink_config` struct and
``supported_interfaces`` in :c:struct:`phylink_pcs` struct.

A PCS is considered eligible when the requested interface mode is present
in both ``pcs_interfaces`` in :c:struct:`phylink_config` struct and
``supported_interfaces`` in :c:struct:`phylink_pcs` struct.

``supported_interfaces`` describes all interface modes supported by the MAC,
whereas ``pcs_interfaces`` identifies the subset that require PCS selection.

For the special implementation where the PCS is internal or part of the MAC
and a dedicated driver is not needed, it's possible to leave the implementation
of the PCS to the MAC driver and just implement the ``num_possible_pcs``
value and ``fill_available_pcs`` helper  function in
:c:struct:`phylink_config` referencing the local :c:struct:`phylink_pcs`
struct allocated from the MAC driver.

.. _pcs_consumer:

Using PCS Devices
-----------------

It's mandatory to either implement the ``mac_select_pcs`` callback
of :c:struct:`phylink_mac_ops` or ``num_possible_pcs`` and ``fill_available_pcs``
of :c:struct:`phylink_config` to use a PCS for a MAC.

The fwnode implementation exposes simple helpers to parse the PCS from
the fwnode :c:func:`fwnode_phylink_pcs_count` and
:c:func:`fwnode_phylink_pcs_parse`. The :c:func:`fwnode_phylink_pcs_count` helper
takes the fwnode where the ``pcs-handle`` should be parsed and return the
number of PCS entries described in the fwnode.
The :c:func:`fwnode_phylink_pcs_parse` helper takes three arguments,
the fwnode where the ``pcs-handle`` should be parsed, an allocated array
of :c:struct:`phylink_pcs` pointer where to put the parsed PCS from the fwnode
and the maximum number of PCS to parse.
Contrary to :c:func:`fwnode_phylink_pcs_count`, :c:func:`fwnode_phylink_pcs_parse`
helper fills the allocated array with ONLY the available PCS and return the
number of available PCS found. PCS that returns -ENODEV will be skipped and
won't be inserted in the allocated array.

A phylink instance may use multiple PCS devices. The maximum number is reported
through ``num_possible_pcs``.

It's mandatory to specify for what interface a PCS is needed. This can be done
by filling the ``pcs_interfaces`` in :c:struct:`phylink_config` struct.
If the requested interface mode is not present in this bitmask, phylink does
not search for a PCS for  that specific mode. (example MAC doesn't need a PCS
for SGMII but require one for USXGMII)

With the use of the :c:func:`fwnode_phylink_pcs_parse` a common implementation
is the following::

   static int mac_fill_available_pcs(struct phylink_config *config,
   				                      struct phylink_pcs **available_pcs,
					                      unsigned int num_possible_pcs)
   {
   	struct device *dev = config->dev;

   	return fwnode_phylink_pcs_parse(dev_fwnode(dev), available_pcs,
						                    num_possible_pcs);
   }

   static int mac_setup_phylink(struct net_device *netdev)
   {
      struct phylink_config *config;

      // ...

      config->dev = &netdev->dev;

      // ...

      // Parse possible PCS and fill num_possible_pcs.
      config->num_possible_pcs = fwnode_phylink_pcs_count(dev_fwnode(&netdev->dev));
      config->fill_available_pcs = mac_fill_available_pcs;

      __set_bit(PHY_INTERFACE_MODE_INTERNAL, config->supported_interfaces);
      __set_bit(PHY_INTERFACE_MODE_SGMII, config->supported_interfaces);
      __set_bit(PHY_INTERFACE_MODE_1000BASEX, config->supported_interfaces);
      __set_bit(PHY_INTERFACE_MODE_USXGMII, config->supported_interfaces);

      // PCS required only for USXGMII
      __set_bit(PHY_INTERFACE_MODE_USXGMII, config->pcs_interfaces);

      phylink = phylink_create(config, //...

It's worth to mention that it's phylink code that takes care of allocating
the array of :c:struct:`phylink_pcs` pointer for ``fill_available_pcs``
callback based on the value set in ``num_possible_pcs`` for
:c:struct:`phylink_config` struct.

The ``fill_available_pcs`` callback must not write more than
``num_possible_pcs`` entries. The third argument may be used to validate
that there is enough space to fill all the available PCS in the passed array
of :c:struct:`phylink_pcs` pointer.

The ``fill_available_pcs`` callback is called only on :c:func:`phylink_create`
and is used only to compose the initial available PCS list. Ownership of PCS
is held by phylink.

.. _pcs_producer:

Writing PCS Drivers
-------------------

To write a PCS driver, first implement :c:struct:`phylink_pcs_ops`. Then,
register your PCS in your probe function using :c:func:`fwnode_pcs_add_provider`.
The :c:func:`fwnode_pcs_add_provider` takes three arguments, the fwnode where
the PCS provider should be registered to, a xlate function to return the requested
PCS based on ``#pcs-cells`` and a pointer to reference private data for the xlate
function.

The PCS will then be registered to a global list of PCS provider that the
PCS fwnode implementation will use to parse it.

For the simple case where the PCS driver expose a single PCS,
:c:func:`fwnode_pcs_simple_xlate` can be used as the xlate function.

You must call :c:func:`fwnode_pcs_del_provider` from your remove function
on driver detach.

A devm variant, :c:func:`devm_fwnode_pcs_add_provider`, is available to
automatically release the PCS provider on driver detach.

It's worth to mention that xlate function MUST reference already allocated
:c:struct:`phylink_pcs` pointer and MUST NOT dynamically allocate new PCS.
The returned pointer is used to identify the PCS across provider lookup and
removal notifications.

Late PCS registration handling
------------------------------

It's possible that a PCS becomes available after the MAC finished probing.
Contrary to the usual producer/consumer implementation, when a PCS is not
registered and can't be found, the fwnode parser helper returns ``-ENODEV``
instead of ``-EPROBE_DEFER``.

This is to prevent race condition with particular devices that register
MAC and PCS with USB or PCIe and require the MAC to be registered before
the PCS.

The phylink logic correctly handle this special case and keep the phylink
instance in a fail condition.

The PCS fwnode implementation provides a notifier to which each phylink
instance with a non-empty ``pcs_interfaces`` in :c:type:`phylink_config`
registers. When a new PCS provider is registered, the notifier is called
triggering the :c:func:`pcs_provider_notify` function.

Function :c:func:`pcs_provider_notify` will check if the just added PCS
should be used by the phylink instance. If it should be used then,
it's added to the internal list of available PCS and a phylink major
config is forced.

If a phylink instance was in a failure state, with the just added PCS
now part of the available PCS internal phylink list, provided all other
conditions are satisfied, the configuration is retried and the failure
condition is cleared.

API Reference
-------------

.. kernel-doc:: include/linux/phylink.h
   :identifiers: phylink_pcs

.. kernel-doc:: include/linux/pcs/pcs.h
   :internal:

.. kernel-doc:: include/linux/pcs/pcs-provider.h
   :internal:
