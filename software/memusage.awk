# Summarize flash and static RAM usage from an SDCC/ASxxxx linker .map file.
# The map repeats an area header when a listing spans pages, hence the
# "seen" guards. Sizes are taken from the decimal column of the area header.

BEGIN {
    flash_max = flash_size + 0
    ram_max = ram_size + 0
}

$1 ~ /^(HOME|GSINIT|GSFINAL|CONST|INITIALIZER|CODE)$/ && !rom_seen[$1]++ {
    rom[$1] = $5 + 0
    rom_used += $5 + 0
}

$1 ~ /^(DATA|INITIALIZED)$/ && !ram_seen[$1]++ {
    ram[$1] = $5 + 0
    ram_used += $5 + 0
}

END {
    printf "  FLASH %6d / %6d B (%4.1f%%)  %6d B free\n",
        rom_used, flash_max, 100.0 * rom_used / flash_max, flash_max - rom_used
    printf "        code %d, const %d, init %d, startup %d\n",
        rom["CODE"], rom["CONST"], rom["INITIALIZER"],
        rom["HOME"] + rom["GSINIT"] + rom["GSFINAL"]
    printf "  RAM   %6d / %6d B (%4.1f%%)  %6d B free (static)\n",
        ram_used, ram_max, 100.0 * ram_used / ram_max, ram_max - ram_used
    printf "        data %d, initialized %d\n",
        ram["DATA"], ram["INITIALIZED"]
}
