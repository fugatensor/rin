#!/usr/bin/env python3
"""Build and validate a deterministic, dependency-free VS Code VSIX."""
from pathlib import Path
import hashlib
import json
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = json.loads((ROOT / 'package.json').read_text(encoding='utf-8'))
VSIX_NS = 'http://schemas.microsoft.com/developer/vsx-schema/2011'
ET.register_namespace('', VSIX_NS)


def build():
    runtime = [ROOT / name for name in ('package.json', 'extension.js', 'README.md', 'LICENSE', 'language-configuration.json')]
    for directory in ('lib', 'server', 'syntaxes', 'snippets'):
        runtime.extend(file for file in (ROOT / directory).rglob('*') if file.is_file())
    for file in runtime:
        if not file.is_file():
            raise SystemExit(f'Missing runtime file: {file}')
        if file.suffix == '.json':
            json.loads(file.read_text(encoding='utf-8'))
    for entry in [PACKAGE['main'], PACKAGE['contributes']['languages'][0]['configuration'],
                  PACKAGE['contributes']['grammars'][0]['path'], PACKAGE['contributes']['snippets'][0]['path']]:
        if not (ROOT / entry).is_file():
            raise SystemExit(f'Missing contributed resource: {entry}')

    manifest = ET.Element(f'{{{VSIX_NS}}}PackageManifest', {'Version': '2.0.0'})
    metadata = ET.SubElement(manifest, 'Metadata')
    ET.SubElement(metadata, 'Identity', {'Language': 'en-US', 'Id': PACKAGE['name'],
                                      'Version': PACKAGE['version'], 'Publisher': PACKAGE['publisher']})
    ET.SubElement(metadata, 'DisplayName').text = PACKAGE['displayName']
    ET.SubElement(metadata, 'Description', {'{http://www.w3.org/XML/1998/namespace}space': 'preserve'}).text = PACKAGE['description']
    ET.SubElement(metadata, 'Categories').text = ','.join(PACKAGE['categories'])
    ET.SubElement(metadata, 'Tags').text = 'rin,language,lsp'
    properties = ET.SubElement(metadata, 'Properties')
    for name, value in {
        'Microsoft.VisualStudio.Code.Engine': PACKAGE['engines']['vscode'],
        'Microsoft.VisualStudio.Code.ExtensionKind': 'workspace',
        'Microsoft.VisualStudio.Code.LocalizedLanguages': '',
        'Microsoft.VisualStudio.Code.PreRelease': 'false',
    }.items():
        ET.SubElement(properties, 'Property', {'Id': name, 'Value': value})
    installation = ET.SubElement(manifest, 'Installation')
    ET.SubElement(installation, 'InstallationTarget', {'Id': 'Microsoft.VisualStudio.Code'})
    ET.SubElement(manifest, 'Dependencies')
    assets = ET.SubElement(manifest, 'Assets')
    for kind, name in [('Microsoft.VisualStudio.Code.Manifest', 'package.json'),
                       ('Microsoft.VisualStudio.Services.Content.Details', 'README.md'),
                       ('Microsoft.VisualStudio.Services.Content.License', 'LICENSE')]:
        ET.SubElement(assets, 'Asset', {'Type': kind, 'Path': 'extension/' + name, 'Addressable': 'true'})

    content_types = ET.Element('Types', {'xmlns': 'http://schemas.openxmlformats.org/package/2006/content-types'})
    for extension, content_type in {'json': 'application/json', 'js': 'application/javascript',
                                  'md': 'text/markdown', 'vsixmanifest': 'text/xml'}.items():
        ET.SubElement(content_types, 'Default', {'Extension': extension, 'ContentType': content_type})
    ET.SubElement(content_types, 'Override', {'PartName': '/extension/LICENSE', 'ContentType': 'text/plain'})

    output = ROOT / 'dist' / f"{PACKAGE['name']}-{PACKAGE['version']}.vsix"
    output.parent.mkdir(exist_ok=True)
    entries = {
        'extension.vsixmanifest': ET.tostring(manifest, encoding='utf-8', xml_declaration=True),
        '[Content_Types].xml': ET.tostring(content_types, encoding='utf-8', xml_declaration=True),
    }
    entries.update({'extension/' + file.relative_to(ROOT).as_posix(): file.read_bytes() for file in runtime})
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        names = set(archive.namelist())
        assert len(names) == len(entries)
        assert all(not name.startswith('/') and '..' not in Path(name).parts for name in names)
        manifest_xml = ET.fromstring(archive.read('extension.vsixmanifest'))
        ET.fromstring(archive.read('[Content_Types].xml'))
        for asset in manifest_xml.findall(f'.//{{{VSIX_NS}}}Asset'):
            assert asset.attrib['Path'] in names
        assert json.loads(archive.read('extension/package.json')) == PACKAGE
    print(f'{output}\n{len(entries)} entries, {output.stat().st_size} bytes\nsha256 {hashlib.sha256(output.read_bytes()).hexdigest()}')


if __name__ == '__main__':
    build()
