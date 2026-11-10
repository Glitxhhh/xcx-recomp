"""Check author-guide links and keep the complete runnable Lua example byte-consistent."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
for p in (ROOT/'docs/mods').glob('*.md'):
 for link in re.findall(r'\]\(([^)]+)\)',p.read_text()):
  if '://' not in link:assert (p.parent/link.split('#')[0]).exists(),(p,link)
text=(ROOT/'docs/mods/02-first-mod.md').read_text();code=re.search(r'```lua\n(.*?)```',text,re.S)[1];assert code==(ROOT/'examples/mods/hello/main.lua').read_text()
assert 'version=0.1.0' in (ROOT/'examples/mods/hello/mod.ini').read_text()
print('Author guides: links and runnable source example match')
