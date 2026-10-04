# Project patches

Create one directory per modified external repository, using its ID from
`upstreams.json`. Each directory contains a `series` file and its numbered patches.
An empty `series` is valid before the first patch; it represents the unchanged pin.
Do not create a fictional patch or pick a base commit just to fill the template.

Commands and source configuration are in [the patching guide](../documentation/patching.md).
