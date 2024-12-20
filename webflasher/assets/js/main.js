document.addEventListener('DOMContentLoaded', () => {
    const button = document.querySelector('esp-web-install-button');
    // button.manifest = `manifest.json`;

    // For GitHub Pages, use the full path to your manifest
    button.manifest = `${window.location.origin}/help-button-firmware/manifest.json`;
  });