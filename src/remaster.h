#pragma once

namespace rg::remaster
{

// Reads RG_REMASTER (0 or 1), overriding the saved menu setting for this run.
void install();

// Called by the F1 menu (Display > Remaster mode), live-switchable.
void setEnabled(bool on);

bool enabled();

} // namespace rg::remaster
