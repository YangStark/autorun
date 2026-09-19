#!/usr/bin/env python3
"""Keep the native capability probe aligned with the pinned DXVK source."""
from pathlib import Path
import re

probe = Path(__file__).resolve().parents[1]
header = (probe / 'source/dxvk_requirements.h').read_text().replace('\\\n', '')
source = probe / 'vendor/dxvk/src/dxvk/dxvk_device_info.cpp'
if not source.is_file():
    raise SystemExit('Fetch the pinned source with tools/build-dxvk.py before this check')
features = source.read_text()
for version, prefix in (('10', 'core.features'), ('11', 'vk11'), ('12', 'vk12'), ('13', 'vk13')):
    required = set(re.findall(r'ENABLE_FEATURE\(' + re.escape(prefix) + r', (\w+), true\)', features))
    macro = re.search(r'^#define NX_DXVK_FEATURES_' + version + r'\(X\)(.*)$', header, re.M)
    actual = set(re.findall(r'X\((\w+)\)', macro.group(1)))
    assert actual == required, (version, 'missing', required - actual, 'extra', actual - required)

runtime = (probe / 'source/vulkan_probe.c').read_text()
for feature in re.findall(r'ENABLE_EXT_FEATURE\(\w+, (\w+), true\)', features):
    assert re.search(r'FEATURE\(&\w+, ' + feature + r'\)', runtime), feature
    assert re.search(r'\.' + feature + r' = VK_TRUE', runtime), feature
for extension in ('VK_KHR_SWAPCHAIN_EXTENSION_NAME', 'VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME'):
    assert runtime.count(extension) >= 2, extension
assert 'pEnabledFeatures = &enabled' in runtime
assert 'pNext = &etf' in runtime
assert 'if (missing) return;' in runtime
print('PASS: DXVK required core/extension features are checked and enabled in the native probe')
