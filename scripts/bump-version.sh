#!/usr/bin/env bash
# Bump version for a specific board using git tag prefixes.
#
# Usage: bump-version.sh <board-name> <fragment>
#   board-name: e.g. waveshare-86-panel
#   fragment:   major | minor | patch | prerelease
#
# Outputs the new version string to stdout.
# Creates no tags — that is the caller's responsibility.

set -euo pipefail

BOARD="${1:?Usage: bump-version.sh <board-name> <fragment>}"
FRAGMENT="${2:?Usage: bump-version.sh <board-name> <fragment>}"

# Fetch all tags matching this board prefix
PREFIX="${BOARD}-"
mapfile -t TAGS < <(git tag -l "${PREFIX}*" 2>/dev/null | sed "s/^${PREFIX}//" | sort -V)

if [[ ${#TAGS[@]} -eq 0 ]]; then
    # No existing tags for this board — start at 0.0.1 (or 0.0.1-dev.1 for prerelease)
    if [[ "${FRAGMENT}" == "prerelease" ]]; then
        echo "0.0.1-dev.1"
    else
        echo "0.0.1"
    fi
    exit 0
fi

# Pick the latest version
LATEST="${TAGS[-1]}"

# Parse version: X.Y.Z or X.Y.Z-dev.N
if [[ "${LATEST}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-dev\.([0-9]+))?$ ]]; then
    MAJOR="${BASH_REMATCH[1]}"
    MINOR="${BASH_REMATCH[2]}"
    PATCH="${BASH_REMATCH[3]}"
    PRE="${BASH_REMATCH[5]:-}"
else
    echo "ERROR: Cannot parse version '${LATEST}'" >&2
    exit 1
fi

case "${FRAGMENT}" in
    major)
        MAJOR=$((MAJOR + 1))
        MINOR=0
        PATCH=0
        echo "${MAJOR}.${MINOR}.${PATCH}"
        ;;
    minor)
        MINOR=$((MINOR + 1))
        PATCH=0
        echo "${MAJOR}.${MINOR}.${PATCH}"
        ;;
    patch)
        # If current is a prerelease, promote to release (strip -dev.N)
        if [[ -n "${PRE}" ]]; then
            echo "${MAJOR}.${MINOR}.${PATCH}"
        else
            PATCH=$((PATCH + 1))
            echo "${MAJOR}.${MINOR}.${PATCH}"
        fi
        ;;
    prerelease)
        if [[ -n "${PRE}" ]]; then
            # Already a prerelease — increment the prerelease number
            PRE=$((PRE + 1))
            echo "${MAJOR}.${MINOR}.${PATCH}-dev.${PRE}"
        else
            # Stable version — bump patch and start prerelease
            PATCH=$((PATCH + 1))
            echo "${MAJOR}.${MINOR}.${PATCH}-dev.1"
        fi
        ;;
    *)
        echo "ERROR: Unknown fragment '${FRAGMENT}'. Use major|minor|patch|prerelease" >&2
        exit 1
        ;;
esac
