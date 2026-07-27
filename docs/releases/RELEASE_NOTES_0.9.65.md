
## Second bench round -- Nearby & DX defect fixes

One root cause accounted for three separate reported symptoms:

- **`editHome()` routed every new editor into the new-activation editor.** That function
  maps an `editTarget` back to the screen to return to, and ends with a catch-all
  `if (t >= 720) return SCR_SKEDENTRY;`. The Nearby & DX targets were numbered 900-904,
  so committing OR cancelling any of them -- the APRS centre grid, the ADS-B scatter
  target, and all three feed settings -- landed in the sked entry form. Explicit entries
  for 900/901 (back to their screens) and 902-904 (back to Settings) now precede the
  catch-all.
- **`editTitle` was never set** when opening the APRS grid ('g') and ADS-B target ('t')
  editors, so the edit screen kept whatever title was left from the previous edit -- which
  is why 'g' appeared to open "ADS-B data source". Both now set their own title.
- **'b' for band filtering collided with the global screenshot hotkey.** Moved to 'n'
  (next band); footers updated.
- **APRS returned no stations after the first fetch.** The match cursor for the `"entries"`
  key was declared `static` inside the scan loop, so it retained its value between calls
  and every fetch after the first began matching at the wrong offset. Now a plain local.
- **Direction-biased sampling in both ADS-B and APRS.** Both stopped parsing at the first
  N records in FILE order and only then sorted by distance, so which records survived
  depended on the feed's ordering -- if it isn't distance-sorted, entire compass sectors
  could be absent (reported as "no aircraft to my east"). Both now scan the whole response
  and retain the nearest N, which is order-independent.
- **Settings rows moved** from the end of Network / data to sit with the other
  data-service rows.
