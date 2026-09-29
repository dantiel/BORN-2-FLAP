"""JPG delivery encoding only; retain linear data bytes, disable chroma subsampling."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path('.setup/texture-deps').resolve()))
from PIL import Image, ImageCms
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('Unreal/Born2Flap/Saved/ShiomoriSource')
srgb=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
for material,source in [('sand_beach','sand-albedo.png'),('white_panel','white-albedo.png')]:
 with Image.open(root/'generated-originals'/source) as image:
  image.convert('RGB').resize((2048,2048),Image.Resampling.LANCZOS).save(root/material/'Diffuse.jpg',quality=100,subsampling=0,optimize=True,icc_profile=srgb)
for folder in ('ocean_waves','sand_beach','white_panel'):
 for path in (root/folder).glob('*.png'):
  with Image.open(path) as image:
   image.convert('RGB').save(path.with_suffix('.jpg'),quality=100,subsampling=0,optimize=True)
   print(path.with_suffix('.jpg'))
