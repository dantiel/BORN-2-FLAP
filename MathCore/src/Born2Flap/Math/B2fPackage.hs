-- | Folder-level IO for @.b2f-model@ packages: writing/reading the
-- @manifest.json@ inside a package directory.
--
-- This is deliberately SEPARATE from Born2Flap.Math.B2fModel (the pure codec)
-- so that the foreign library never links @directory@/@filepath@: the FFI load
-- path (Born2Flap.Math.ModelCatalog) only needs the pure manifest codec, and
-- every extra package on the Windows shared-library link pushes it closer to
-- the PE export-table ceiling under @--export-all-symbols@. Tools and the
-- exporter use this module; the runtime DLL does not.
module Born2Flap.Math.B2fPackage
  ( saveModelPackage
  , loadModelPackage
  ) where

import System.Directory (createDirectoryIfMissing, doesFileExist)
import System.FilePath ((</>))

import Born2Flap.Math.B2fModel (ModelManifest, encodeManifest, decodeManifest)
import Born2Flap.Math.Json (parseJson, renderJson)

-- | Write a model package: creates the folder and writes @manifest.json@.
-- Referenced art assets are the caller's responsibility (the Haskell core only
-- owns the physics manifest; the Unreal host/tools own the art).
saveModelPackage :: FilePath -> ModelManifest -> IO ()
saveModelPackage dir mm = do
  createDirectoryIfMissing True dir
  writeFile (dir </> "manifest.json") (renderJson (encodeManifest mm) ++ "\n")

-- | Read a model package's @manifest.json@ and parse it.
loadModelPackage :: FilePath -> IO (Either String ModelManifest)
loadModelPackage dir = do
  let path = dir </> "manifest.json"
  exists <- doesFileExist path
  if not exists
    then pure (Left ("no manifest.json in " ++ dir))
    else do
      text <- readFile path
      pure (parseJson text >>= decodeManifest)
