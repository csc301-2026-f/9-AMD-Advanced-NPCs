#!/bin/sh
# Packages the Schola plugin source as Schola-<label>.zip, ready to unzip into a project's Plugins/ folder.
# Used by .github/workflows/deploy.yml for the dev and release environments, and runnable locally.
#
#   Tools/Release/package-plugin.sh <label> [out-dir]      # e.g. Tools/Release/package-plugin.sh D4 Saved/Release
#
# Only tracked files are packaged. Git LFS files (the gRPC libraries) must be pulled first; the script
# fails if any packaged file is still an LFS pointer or if a module in Schola.uplugin has no Build.cs.
set -eu

label=${1:?usage: package-plugin.sh <label> [out-dir]}
out=${2:-Saved/Release}
root=$(git rev-parse --show-toplevel)
cd "$root"

stage="$out/stage/Schola"
zip_path="$out/Schola-$label.zip"
rm -rf "$out/stage" "$zip_path"
mkdir -p "$stage"

# Plugin runtime and its docs. Examples, Docs, Prototypes and deliverables stay in the repository.
paths="Schola.uplugin Config Resources Source LICENSE.txt README.md README-Schola.md DEVELOPMENT.md CITATION.cff"
git ls-files -z -- $paths > "$out/stage/files.txt"
tar -cf - --null -T "$out/stage/files.txt" | tar -xf - -C "$stage"

fail=0

# The staged copy must hold every tracked file.
expected=$(tr -cd '\0' < "$out/stage/files.txt" | wc -c | tr -d ' ')
staged=$(find "$stage" -type f | wc -l | tr -d ' ')
if [ "$staged" -ne "$expected" ]; then
    echo "FAIL staged $staged files, expected $expected" >&2
    fail=1
fi
rm "$out/stage/files.txt"

# Every module the plugin declares must have its build rules.
modules=$(python3 -c "import json; print(' '.join(m['Name'] for m in json.load(open('Schola.uplugin'))['Modules']))")
for module in $modules; do
    if [ ! -f "$stage/Source/$module/$module.Build.cs" ]; then
        echo "FAIL module $module has no Source/$module/$module.Build.cs" >&2
        fail=1
    fi
done

# An LFS pointer in the package means the real library was never downloaded.
pointers=$(grep -rlI --max-count=1 '^version https://git-lfs.github.com/spec/v1' "$stage" || true)
if [ -n "$pointers" ]; then
    echo "FAIL Git LFS pointers instead of files (run git lfs pull):" >&2
    echo "$pointers" | sed 's/^/  /' >&2
    fail=1
fi

[ "$fail" -eq 0 ] || exit 1

commit=$(git rev-parse HEAD)
cat > "$stage/BUILD-INFO.txt" <<EOF
Schola plugin package: $label
Commit: $commit
Built: $(date -u +%Y-%m-%dT%H:%M:%SZ)
Install: unzip into <YourProject>/Plugins/ so that Plugins/Schola/Schola.uplugin exists, then build with Unreal Engine 5.8.
EOF

(cd "$out/stage" && zip -qr -X "../$(basename "$zip_path")" Schola)
rm -rf "$out/stage"

files=$(unzip -Z1 "$zip_path" | grep -vc '/$')
size=$(du -h "$zip_path" | cut -f1)
echo "OK $zip_path ($files files, $size, $(echo "$modules" | wc -w | tr -d ' ') modules, commit ${commit%"${commit#????????????}"})"
