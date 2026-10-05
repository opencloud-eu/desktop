#!/usr/bin/env python3
"""Exercise a signed, disposable FileProvider host without using real accounts."""
import argparse
import base64
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import uuid
import sys
import threading


def run(*command, **kwargs):
    return subprocess.run([str(value) for value in command], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extension', type=Path, required=True)
    parser.add_argument('--sign-id', required=True)
    parser.add_argument('--team-id', required=True)
    parser.add_argument('--interactive', action='store_true', help='Wait for user to enable the isolated provider, then test Finder I/O')
    parser.add_argument('--bundle-id', help='Reuse an isolated test identifier between interactive runs')
    parser.add_argument('--probe-missing-extension', action='store_true', help='Also inspect native discovery after removing a registered extension')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    bundle_id = args.bundle_id or 'eu.opencloud.desktop.review.' + uuid.uuid4().hex
    if not bundle_id.startswith('eu.opencloud.desktop.review.'):
        parser.error('--bundle-id must use the isolated eu.opencloud.desktop.review. prefix')
    group = args.team_id + '.' + bundle_id
    domain_id = 'review-' + str(uuid.uuid4())
    space_id = 'a0ca6a90-a365-4782-871e-d44447bbc668$a0ca6a90-a365-4782-871e-d44447bbc668'
    project_domain_id = domain_id.removeprefix('review-') + '.space.' + base64.urlsafe_b64encode(space_id.encode()).decode().rstrip('=')
    sys.path.insert(0, str(repo / 'test/macos'))
    from mock_webdav import Fixture
    fixture = Fixture()
    fixture.add_domain(domain_id)
    fixture.add_domain(domain_id + "-second")
    server_thread = threading.Thread(target=fixture.serve_forever, daemon=True)
    server_thread.start()
    server_url = f'http://127.0.0.1:{fixture.server_port}'
    lsregister = '/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister'
    with tempfile.TemporaryDirectory(prefix='opencloud-signed-review-') as temporary:
        root = Path(temporary)
        app = root / 'OpenCloudReview.app'
        contents = app / 'Contents'
        (contents / 'MacOS').mkdir(parents=True)
        (contents / 'PlugIns').mkdir()
        executable = contents / 'MacOS/Probe'
        run('xcrun', 'swiftc', '-parse-as-library', '-import-objc-header',
            repo / 'shell_integration/MacOSX/OpenCloudFinderExtension/FileProviderExt/Services/ClientCommunicationProtocol.h',
            repo / 'test/macos/signed_fileprovider.swift', '-o', executable)
        (contents / 'Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': bundle_id, 'CFBundleExecutable': 'Probe', 'AppGroupIdentifier': group,
            'CFBundleName': 'OpenCloud Isolated Test', 'CFBundlePackageType': 'APPL',
            'CFBundleVersion': '1', 'CFBundleShortVersionString': '1.0', 'LSUIElement': True}))
        # Check native discovery for a clean install with no bundled provider.
        run('codesign', '--force', '--timestamp=none', '--sign', args.sign_id, app)
        run(executable, 'domains', domain_id, timeout=20)
        extension = contents / 'PlugIns/FileProviderExt.appex'
        shutil.copytree(args.extension, extension)
        info_path = extension / 'Contents/Info.plist'
        info = plistlib.loads(info_path.read_bytes())
        info['CFBundleIdentifier'] = bundle_id + '.FileProviderExt'
        info['CFBundleDisplayName'] = 'OpenCloud Isolated Test'
        info['CFBundleVersion'] = '1'
        info['CFBundleShortVersionString'] = '1.0'
        info['AppGroupIdentifier'] = group
        info['NSAppTransportSecurity'] = {'NSAllowsLocalNetworking': True}
        info['NSExtension']['NSExtensionFileProviderDocumentGroup'] = group
        info_path.write_bytes(plistlib.dumps(info))
        for name, path in [('extension', extension), ('host', app)]:
            entitlements = {'com.apple.security.application-groups': [group]}
            if name == 'extension':
                entitlements.update({'com.apple.security.app-sandbox': True,
                                     'com.apple.security.network.client': True})
            entitlements_path = root / (name + '.plist')
            entitlements_path.write_bytes(plistlib.dumps(entitlements))
            run('codesign', '--force', '--timestamp=none', '--sign', args.sign_id,
                '--entitlements', entitlements_path, path)
        foreign = root / 'ForeignCaller'
        run('xcrun', 'swiftc', '-parse-as-library', '-import-objc-header',
            repo / 'shell_integration/MacOSX/OpenCloudFinderExtension/FileProviderExt/Services/ClientCommunicationProtocol.h',
            repo / 'test/macos/untrusted_fileprovider.swift', '-o', foreign)
        run('codesign', '--force', '--timestamp=none', '--sign', args.sign_id,
            '--identifier', bundle_id + '.foreign', foreign)
        run(lsregister, '-f', app)
        run('pluginkit', '-a', extension)
        run('pluginkit', '-e', 'use', '-i', bundle_id + '.FileProviderExt')
        try:
            run(executable, 'interactive' if args.interactive else 'exercise', domain_id, server_url, foreign, timeout=360)
            if args.probe_missing_extension:
                run(executable, 'register-only', domain_id, timeout=20)
                run('pluginkit', '-r', extension)
                saved_extension = root / 'SavedFileProviderExt.appex'
                extension.rename(saved_extension)
                try:
                    run('codesign', '--force', '--timestamp=none', '--sign', args.sign_id,
                        '--entitlements', root / 'host.plist', app)
                    run(lsregister, '-f', app)
                    run(executable, 'domains', domain_id, timeout=20)
                finally:
                    saved_extension.rename(extension)
                    run('codesign', '--force', '--timestamp=none', '--sign', args.sign_id,
                        '--entitlements', root / 'host.plist', app)
                    run(lsregister, '-f', app)
                    run('pluginkit', '-a', extension)
        finally:
            try:
                run(executable, 'cleanup', domain_id, timeout=20)
                run(executable, 'cleanup', domain_id + '-second', timeout=20)
                run(executable, 'cleanup-project', domain_id, project_domain_id, timeout=20)
            finally:
                run('pluginkit', '-r', extension)
                run(lsregister, '-u', app)
                # Delete only this test's record, including failure paths where the
                # extension could not acknowledge cleanup before native removal.
                subprocess.run(['security', 'delete-generic-password', '-s',
                    'eu.opencloud.desktop.FileProviderExt.credentials', '-a', domain_id],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
                subprocess.run(['security', 'delete-generic-password', '-s',
                    'eu.opencloud.desktop.FileProviderExt.credentials', '-a', domain_id + '-second'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
                print('Fixture requests:', fixture.requests[-30:])
                fixture.shutdown()
                server_thread.join()
                fixture.server_close()


if __name__ == '__main__':
    main()
