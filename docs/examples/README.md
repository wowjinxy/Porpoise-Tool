# Redacted recovery examples

These files contain no game, SDK, or OneTri binary data.

- `recovery-workbench.porpoise.json` demonstrates a map-backed main target and
  a disabled prepared-DTK REL target. Copy it beside the intended `input`,
  `maps`, `catalogs`, `abi`, and `generated` folders before editing paths.
- `sdk-allowlist.example.json` demonstrates explicit mapless catalog
  classification. Replace every selector and identity with reviewed local
  values before running `porpoise-sdk-catalog`.

Do not commit generated local catalogs or reports when their provenance or
names disclose proprietary inputs. Catalog signature digests are
nonrecoverable metadata, but repository policy still determines whether a
particular locally derived catalog may be shared.
