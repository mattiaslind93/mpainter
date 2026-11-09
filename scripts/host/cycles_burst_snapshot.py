"""Fixed named burst-profile source admission; no compiler or runtime calls.

The final merged collector remains the only build/header-freeze implementation.
This module supplies explicit profile data and exact reviewed fixture admission;
it never changes another module's constants or accepts production overrides.
"""
import hashlib
import json
from pathlib import Path

PROFILE = "held-picker-burst-6s-v1"
BE_COMMIT = "970312ea7da686a6b25b9da0e107d5c47e8c2baf"
BE_SOURCES = {
    "native/tests/CyclesLiveCompletionProbe.hh": "c232d283e5923e2d5d25aea3bff5ec0dd65731943c390729569505ab466a2615",
    "native/tests/CyclesLiveCompletionProbe.cc": "272915d1df23974da2b1841d9b0d0679888c048b3525609d60f28761b79b81d4",
    "native/tests/CyclesLiveUdimArtist.hh": "8ee2d24f287eb296591dbff751d5f76a797de796784691bb67e81ef8c748989e",
    # Re-pinned 2026-11-09 with the owner's approval: the only change since the reviewed
    # 0a1f9ece... is the include of GuiFixDemo.hh, now in native/harness.
    "native/tests/cycles_live_udim_artist_main.cc": "b887a76512721549537c820aad943eafbc6365d3fac4ec1a1d6f048b1d29ef2a",
    "native/tests/cycles_live_udim_artist_test.cc": "5506caf08b3e9313506875849942c0c3a30541b1a064500beb238638cce0b818",
}
BURST_SOURCES = {
    "native/tests/CyclesLiveBurst.hh",
    "native/tests/cycles_live_burst_main.cc",
    "native/tests/cycles_live_burst_test.cc",
}
TEST_SOURCES = set(BE_SOURCES) | BURST_SOURCES
COMPILED_TEST_NAMES = ("cycles_live_burst_main.cc", "cycles_live_burst_test.cc")
FIXTURE = "native/tests/cycles_live_udim_artist_test.cc"
FRIEND = b" friend class CyclesLiveBurst; // Separate test actor reuses this exact fixture; no production hook.\n"


def validate_sources(repo):
    repo = Path(repo)
    records = {}
    for relative, expected in BE_SOURCES.items():
        raw = (repo / relative).read_bytes()
        original = raw
        preflight_removed = False
        if relative == FIXTURE:
            if raw.count(FRIEND) != 1:
                raise ValueError("Burst fixture requires exactly the reviewed friend line")
            original = raw.replace(FRIEND, b"", 1)
            # Admit only the explicitly removed memory refusal, retaining the
            # historical collector hash as the comparison baseline.
            removed = b'   require_texture_ram(std::uint64_t(resolution)*resolution*16*4+16ULL*1024*1024,"Controlled diagnostic full tile hashes");'
            anchor = b'  if(!diagnostic_readback){\n\n   diagnostic_stage='
            if original.count(anchor) == 1:
                original = original.replace(anchor, b'  if(!diagnostic_readback){\n' + removed + b'\n   diagnostic_stage=', 1)
                preflight_removed = True
        if hashlib.sha256(original).hexdigest() != expected:
            raise ValueError("Burst fixture differs from final reviewed BE collector: " + relative)
        records[relative] = {"sha256": hashlib.sha256(raw).hexdigest(), "reviewed_be_sha256": expected,
                             "reviewed_be_commit": BE_COMMIT, "friend_only": relative == FIXTURE and not preflight_removed,
                             "memory_preflight_removed": preflight_removed}
    for relative in BURST_SOURCES:
        records[relative] = {"sha256": hashlib.sha256((repo / relative).read_bytes()).hexdigest()}
    # The constructor has no default hook; passing {} is intentional and keeps
    # the ordinary fixture's controlled observer entirely inactive here.
    case = (repo / "native/tests/cycles_live_burst_test.cc").read_text()
    header = (repo / "native/tests/CyclesLiveBurst.hh").read_text()
    if ("std::move(edit),CyclesFrameHook{})" not in case or "using CyclesBurstFrameHook=CyclesFrameHook;" not in header or
            any("#define BTS_BA_CONTROLLED_PROFILE_TEST" in (repo / relative).read_text() for relative in BURST_SOURCES)):
        raise ValueError("Burst has not adopted the final collector callback API")
    return records


def overlay(snapshot, repo):
    original = json.loads((Path(snapshot) / "integrated-snapshot.json").read_text())["native_source_sha256"]
    return {relative: {**record, "original_sha256": original.get(relative)}
            for relative, record in sorted(validate_sources(repo).items())}


def validate_overlay(snapshot, repo, records):
    if set(records) != TEST_SOURCES:
        raise ValueError("Undeclared burst source overlay or missing final BE test source")
    if records != overlay(snapshot, repo):
        raise ValueError("Stale or forged burst source overlay")


def validate_controlled_parent(path, admitted, snapshot, default_record):
    """Retain the prior controlled build, including its failed diagnostic identity.

    This validates provenance only; it does not reinterpret the retained
    controlled runtime as successful or substitute it for burst evidence.
    """
    from cycles_live_manifest import sha256
    if path is None:
        raise ValueError("Burst requires the separate controlled diagnostic parent")
    path, snapshot = Path(path).resolve(), Path(snapshot).resolve()
    parent = json.loads(path.read_text())
    if (parent.get("profile") != "controlled-fixed-sampling-v1" or
            Path(parent.get("integrated_snapshot", "")).resolve() != snapshot or
            parent.get("mode") != "udim" or parent.get("sanitized_bridge_only") is not False):
        raise ValueError("Burst controlled parent profile/integration differs")
    overlay_records = parent.get("test_overlay", {})
    if set(overlay_records) != set(BE_SOURCES):
        raise ValueError("Incomplete final BE controlled parent overlay")
    for relative, digest in BE_SOURCES.items():
        info = overlay_records[relative]
        if (info.get("sha256") != digest or parent.get("source_sha256", {}).get(relative) != digest or
                info.get("original_sha256") != admitted["native_source_sha256"].get(relative)):
            raise ValueError("Controlled parent differs from reviewed BE source: " + relative)
    for relative, digest in admitted["native_source_sha256"].items():
        if parent.get("source_sha256", {}).get(relative) != BE_SOURCES.get(relative, digest):
            raise ValueError("Controlled parent production source differs: " + relative)
    def identity(record):
        return Path(record.get("path", "")).resolve(), record.get("sha256")
    parents = parent.get("parents", [])
    identities = {identity(item) for item in parents}
    if identity(default_record) not in identities or (snapshot / "integrated-snapshot.json",
                                                     sha256(snapshot / "integrated-snapshot.json")) not in identities:
        raise ValueError("Controlled parent lacks the exact original integration/default parent")
    builds = []
    for item in parents:
        if sha256(item["path"]) != item["sha256"]:
            raise ValueError("Stale controlled parent evidence")
        if Path(item["path"]).name == "build.json":
            builds.append(json.loads(Path(item["path"]).read_text()))
    if len(builds) != 1:
        raise ValueError("Controlled parent lacks unique build evidence")
    build = builds[0]
    if (build.get("profile") != "controlled-fixed-sampling-v1" or identity(build.get("default_parent", {})) != identity(default_record) or
            build.get("production_units_recompiled") != [] or build.get("fork_patches_reapplied") is not False or
            [unit.get("source") for unit in build.get("compiled_test_units", [])] != [
                "native/tests/cycles_live_udim_artist_main.cc", "native/tests/cycles_live_udim_artist_test.cc",
                "native/tests/CyclesLiveCompletionProbe.cc"]):
        raise ValueError("Controlled parent build is not the reviewed test-only closure")
    binaries = parent.get("binaries", {})
    if len(binaries) != 2 or {Path(name).name for name in binaries} != {"cycles-live-artist", "cycles-cache-cpu"}:
        raise ValueError("Controlled parent binary set differs")
    for name, digest in binaries.items():
        if sha256(name) != digest:
            raise ValueError("Stale controlled parent binary")
    return {"path": str(path), "sha256": sha256(path)}

# Immutable first 3-second negative; paths are relative to its retained attempt.
# No test repeats or reclassifies that run. Its source/binary manifest is separate.
PREVIOUS_EVIDENCE = {
    "cycles-live-burst.json": "47d5b90501f1780bd6bef5a50a25dfd7175ca1afc00c3db13c608f131cdd1453",
    "run.log": "c6e32ae7a688d8c0bb0d9ca087bcddc171d845251ff53c3ff008a201f88cf6de",
    "inconclusive-summary.json": "c11f825c42082dec88a99ae43bdd02cc6272cf8766ff68f087ae9ddfe6aafbd9",
    "provenance/run-manifest.json": "e4a6ac574a22d1a703fff7d70dcc4fe75d3d10883c73f70b9728eafbbc22591f",
}


def validate_previous_attempt(path):
    from cycles_live_manifest import sha256
    if path is None:
        raise ValueError("Six-second cohort requires its retained three-second inconclusive parent")
    root = Path(path).resolve()
    records = []
    for relative, expected in PREVIOUS_EVIDENCE.items():
        target = root / relative
        if sha256(target) != expected:
            raise ValueError("Original three-second evidence changed: " + relative)
        records.append({"path": str(target), "sha256": expected})
    return records
