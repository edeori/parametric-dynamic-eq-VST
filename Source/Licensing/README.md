# Reusable JUCE Licensing Module

This folder is designed to be copied into another JUCE plugin project with minimal edits.

## Files

- `LicenseManager.*`: main public API used by the plugin processor/editor
- `LicenseClient.*`: HTTPS JSON client for activate/validate/refresh/deactivate
- `LicenseStore.*`: local state persistence in the user application data folder
- `MachineFingerprint.*`: stable machine hash generation
- `TrialManager.*`: 15-day trial and clock rollback checks
- `LicenseConfig.*`: config loading from JSON or preprocessor definitions
- `LicenseStatus.h`: shared status enum

## Integration steps

1. Copy `Source/Licensing/` into the target plugin.
2. Add the `.cpp` files to the target build.
3. Provide config via either:
   - an embedded JSON resource loaded with `LicenseConfig::fromJsonString(...)`, or
   - CMake / Projucer preprocessor definitions read by `LicenseConfig::fromPreprocessorDefinitions(...)`.
4. Construct `LicenseManager` in the processor, then call `initialise()` on a non-audio thread before `processBlock()` starts doing work.
5. In `processBlock()`, gate audio with:

```cpp
if (! licenseManager.isAudioEnabled())
{
    buffer.clear();
    return;
}
```

6. In the editor, call:
   - `getDaysRemainingInTrial()`
   - `getUserVisibleStatusMessage()`
   - `startActivation(key)`
   - `deactivate()`

## Required config values

- `companyName`
- `applicationName`
- `productId`
- `productVersion`
- `serverBaseUrl`
- `publicKey`
- `endpoints.activate`
- `endpoints.validate`
- `endpoints.refresh`
- `endpoints.deactivate`
- `trialLengthDays`
- `offlineGracePeriodDays`
- `requestTimeoutMs`

## Example config JSON

```json
{
  "companyName": "Your Company",
  "applicationName": "Your Plugin",
  "productId": "your-plugin",
  "productVersion": "1.0.0",
  "serverBaseUrl": "https://licenses.example.com",
  "publicKey": "PUBLIC_KEY_PLACEHOLDER",
  "trialLengthDays": 15,
  "offlineGracePeriodDays": 14,
  "requestTimeoutMs": 5000,
  "allowUnsignedDevelopmentLicenses": false,
  "endpoints": {
    "activate": "/activate",
    "validate": "/validate",
    "refresh": "/refresh",
    "deactivate": "/deactivate"
  }
}
```

## Expected server API

The client posts JSON to the configurable endpoints with this request shape:

```json
{
  "licenseKey": "...",
  "productId": "...",
  "machineId": "...",
  "pluginVersion": "...",
  "platform": "macOS|Windows",
  "activationId": "...",
  "signedLicense": "..."
}
```

Expected response shape:

```json
{
  "success": true,
  "status": "activated",
  "activationId": "...",
  "signedLicense": "...",
  "expiresAt": null,
  "maxActivations": 2,
  "message": "..."
}
```

## Signed payload format

The client expects `signedLicense` to be a JSON string envelope:

```json
{
  "algorithm": "rsa-sha256",
  "keyId": "default",
  "payload": "{\"activationId\":\"...\",\"expiresAt\":null,\"issuedAt\":\"2026-07-07T00:00:00Z\",\"lastValidatedAt\":\"2026-07-07T00:00:00Z\",\"licenseKey\":\"...\",\"machineId\":\"...\",\"maxActivations\":2,\"message\":\"License active\",\"offlineGraceDays\":14,\"productId\":\"your-plugin\",\"status\":\"activated\"}",
  "signature": "hex-rsa-signature"
}
```

The plugin verifies the SHA-256 digest of `payload` with the configured `publicKey`.
Unsigned `algorithm: "none"` envelopes are only accepted when `allowUnsignedDevelopmentLicenses` is enabled in config.

## Security limitations

- The client can be reverse engineered. Do not embed private signing secrets.
- The client can only enforce what it can verify locally. If an attacker patches the binary, no client-only scheme fully prevents that.
- Local tamper checks are basic integrity checks, not strong anti-crack protection.
- Machine fingerprinting is abstracted so a stronger implementation can replace the current one later.
