# Engineering materials and appearances

This module deliberately separates rendering appearance from physical
engineering material identity.

- `Preset` retains the stable PBR/texture names used by existing models.
- `Profile` is a supplier- and product-form-specific engineering record.
- `icad.material.catalog.v1` preserves minimum, typical, maximum, balance,
  temperature, condition, test method, product form, and datasheet provenance.

Use a physical profile in source without duplicating its appearance:

```icad
MATERIAL compressor_blade
  PROFILE ATI_TI_6AL_4V_GRADE5_BAR
END
```

`PRESET` may still override appearance. A material profile is selection and
traceability data, not a design allowable and not a supplier purchase order.
The catalog says `curated_supplier_profiles_not_exhaustive`; undisclosed
polymer, elastomer, glass, ceramic, and composite constituents remain explicit
instead of being guessed.

Machine-readable access is available through `icad materials-json`,
`icad material-json ID`, `icad.materials`, and `icad.material.inspect`.
The `--class` selector accepts either a top-level class such as `metal` or a
subclass such as `nickel_superalloy`; the catalog response lists both taxonomies.
