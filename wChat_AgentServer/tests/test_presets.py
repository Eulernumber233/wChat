from __future__ import annotations


def test_presets_loaded(preset_store):
    ids = {p.id for p in preset_store.all()}
    assert {"polite_decline", "comfort", "humor_deflect", "formal_business", "flirty"} <= ids


def test_preset_prompt_block_shape(preset_store):
    p = preset_store.get("polite_decline")
    assert p is not None
    block = p.to_prompt_block()
    assert "礼貌拒绝" in block
    assert "风格要点" in block


def test_unknown_preset_returns_none(preset_store):
    assert preset_store.get("nope_not_real") is None
    assert preset_store.get(None) is None
