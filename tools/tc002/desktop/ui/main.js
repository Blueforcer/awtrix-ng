import {connectGranted} from '../shared/adb.js';
import {parsePackage} from '../shared/package.js';
import {Tc002Installer, provisionWifi, validateWifi} from '../shared/install.js';
import {buildResImage, preload} from '../shared/image/index.js';
import {mountInstaller} from './controller.js';
import {createNativeBridge} from './native.js';

const root = document.querySelector('#installer');
try {
  const native = createNativeBridge();
  mountInstaller(root, {
    connectGranted, parsePackage, Tc002Installer, validateWifi, provisionWifi, buildResImage, preload,
    desktop: native.desktop, usb: native.usb,
  });
} catch {
  root.textContent = 'The native installer could not start. Close and reopen the application.';
  root.setAttribute('role', 'alert');
}
