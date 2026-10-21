"""Fail before asset writes when an editor was launched without this project."""
from pathlib import Path
import unreal as u

def require_project():
    expected=Path(__file__).resolve().parents[1]
    actual=Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir())).resolve()
    if actual != expected:
        raise RuntimeError('Wrong Unreal project directory: %s; expected %s. Quote the .uproject argument, including with PowerShell Start-Process. No assets were generated.'%(actual,expected))
    if not hasattr(u,'Born2FlapStaticBird'):
        raise RuntimeError('Born2Flap game module is unavailable. Build Born2FlapEditor and launch the quoted project before generating assets.')
    u.log('B2F_PROJECT_VERIFIED '+str(actual))
