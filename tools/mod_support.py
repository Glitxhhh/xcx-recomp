"""Public mod files shared by release assembly and installation; packages stay opt-in."""
from pathlib import Path

PUBLIC_FILES = (
    'LICENSE', 'THIRD_PARTY_NOTICES.md',
    'runtime/mod_provider_abi.h', 'runtime/mod_provider_service_abi.h',
    'docs/mods/README.md', 'docs/mods/01-start-here.md', 'docs/mods/02-first-mod.md',
    'docs/mods/03-menu-registration.md', 'docs/mods/04-compatibility.md',
    'docs/lua-mods.md', 'docs/native-providers.md', 'docs/provider-actions.md',
    'examples/mods/hello/mod.ini', 'examples/mods/hello/main.lua',
)


def source_files(root, development=False):
    """Source and installation-relative destination; never include mods, settings or profiles."""
    root = Path(root)
    pairs = [(root / name, Path(name)) for name in PUBLIC_FILES]
    pairs.append((root / ('deps/lua/LICENSE.txt' if development else 'licenses/lua-LICENSE.txt'),
                  Path('licenses/lua-LICENSE.txt')))
    return pairs
