.pragma library
.import eu.OpenCloud.resources 1.0 as Resources

// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

function resourcePath(theme, icon, enabled, size) {
    return resourcePath2("opencloud", theme, icon, enabled, size);
}

function resourcePath2(provider, theme, icon, enabled, size) {
    return `image://${provider}?theme=${theme}&icon=${encodeURIComponent(icon)}&enabled=${enabled}&size=${size}`;
}
