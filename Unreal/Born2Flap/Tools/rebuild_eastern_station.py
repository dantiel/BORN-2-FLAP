"""Refresh station and granite surfaces without rewriting coastal scan assets."""
from pathlib import Path
path=Path(__file__).with_name('create_east_shore_assets.py')
source=path.read_text(encoding='utf-8-sig')
header=source.split("for name in ('coast_sand_rocks_02'")[0]
terrain=source[source.index("path='/Game/Shiomori/Materials/M_EastShore'"):]
exec(compile(header+terrain,str(path),'exec'),{'__file__':str(path)})
