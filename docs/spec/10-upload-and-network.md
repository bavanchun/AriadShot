<!--
SPDX-FileCopyrightText: 2026 The AriadShot Authors
SPDX-License-Identifier: GPL-3.0-only
-->

# 10. Upload and Network

Part of the [AriadShot technical specification](README.md). This file specifies every network access AriadShot makes:
the network policy, the three upload providers, the offline build, the translation endpoint, update checks and the
network rules for model downloads. The classes live in `net/` ([02 §13](02-modules-and-interfaces.md)); the model
store lives in `ml/` ([09](09-ml-services.md)). Privacy promises and the threat model are in
[11](11-security-privacy.md).

Parity rows covered: SV-06 (translation), SV-07 (uploads), SH-25 (offline build), SH-26 (updates), ED-11 (upload toast,
network side), OV-31 (upload confirmation), TB-15 (the upload-confirm menu that the offline build removes, §6), ST-07
(the translation-engine section, §7) and the upload lines of TB-08, ED-04, ED-06 and VE-14. Deviations used:
DEV-01, DEV-31, DEV-32, DEV-33, DEV-36, DEV-37.

## 1. Network policy

AriadShot sends nothing that the user did not ask for. `net::NetworkPolicy` is the single gate: every request carries a
purpose, and the policy refuses a request whose purpose is not allowed in this build and situation.

| Purpose | Allowed when | Offline build |
| :--- | :--- | :--- |
| `upload` | the user pressed Upload (or confirmed the upload popover) for a specific item | removed (§6) |
| `accountSignIn` | the user pressed Sign In for Google Drive in Settings | removed (§6) |
| `translate` | the user ran Translate, OCR & Translate, or the `ocr-translate` action | allowed (§6) |
| `updateCheck` | the user chose Check for Updates…, or the automatic check is enabled (§8) | allowed (§6) |
| `modelDownload` | the user accepted the consent sheet for that model ([09](09-ml-services.md)) | disabled; sideload only (DEV-32) |

Rules that hold for every purpose:

- No telemetry, analytics, crash upload, usage ping, remote configuration or "phone home" of any kind exists in any
  build.
- Nothing is fetched at start-up except the automatic update check when it is enabled, and that check never runs more
  than once per scheduled interval (§8).
- Content AriadShot displays never triggers a fetch: sanitised clipboard HTML drops images and external resources
  ([11](11-security-privacy.md)), and no web view exists in the product.
- Requests go through one `QNetworkAccessManager` owned by `net`, on the GUI thread, asynchronously. Bodies are
  streamed from files; no request blocks the GUI thread.
- Every request that `ui/` or `windows/` needs is made by `app` on their behalf ([02 §9](02-modules-and-interfaces.md)),
  so the policy sees every call.

## 2. Uploads: common behaviour

The provider is `uploadProvider`: `imgbb` (default), `gdrive` or `s3` (`macshot/AppDelegate.swift:2190`). Images can
go to all three. Videos and GIFs from the studio go only to Google Drive or S3; with imgbb selected the studio shows
"Video upload requires Google Drive or S3" (`macshot/UI/Editor/VideoEditorWindowController+Export.swift:245-256`).

**Sequence.**

1. **Confirmation.** When `uploadConfirmEnabled` is on (default off), the Upload button opens the upload confirmation
   popover naming the provider first (`macshot/UI/Overlay/OverlayView.swift:8246-8252`); otherwise it uploads at once.
2. **Pre-flight.** An unusable provider fails immediately with MacShot's messages: "Google Drive not signed in", "S3 not
   configured. Check Settings." (`macshot/AppDelegate.swift:2192-2200`), and in the studio "Sign in to Google Drive in Settings" /
   "Configure S3 in Settings". A provider whose AriadShot credentials are missing (§3, §4) shows "not configured".
3. **Toast.** The upload toast (ED-11, [04](04-capture-and-overlay.md)) appears with "Uploading..." and shows progress
   where MacShot shows it: S3 for images; S3 and Drive for studio video uploads ("Uploading to %@... %d%%"); imgbb and
   Drive image uploads show none (`macshot/AppDelegate.swift:2202-2216`).
4. **Payload.** A still uploads as PNG of the final canonical image, whatever `imageFormat` says
   (`macshot/Upload/S3Uploader.swift:78-86`). Drive and S3 name it with the screenshot filename template plus `.png`
   ([07 §5.3](07-storage-history-settings.md)); imgbb names it `Screenshot.png`. A video keeps its file name.
5. **Body staging.** The payload is written once to a staged body file in the scratch directory
   ([07 §5.8](07-storage-history-settings.md)), mode 0600, in 1 MiB chunks, and its SHA-256 is computed in the same pass
   (`macshot/Upload/UploadPayload.swift:20-66`, `macshot/Upload/PreparedUploadBody.swift:5-25`). S3 streams that file
   as the request body. imgbb and Google Drive need a multipart envelope: as in MacShot, a **second** staged file is
   written that contains the envelope with the payload copied between its preamble and trailer (`multipart/form-data`
   for imgbb, `multipart/related` for Drive; `macshot/Upload/PreparedUploadBody.swift:27-48`); it needs no payload
   hash. Every attempt of a request streams its staged file with an explicit `Content-Length`; a multi-gigabyte
   recording is never held in memory. Both staged files are deleted after the last attempt.
6. **Job.** The upload is a job of the export coordinator ([07 §5.6](07-storage-history-settings.md)). It enters the
   publishing state when it starts, because a remote request can commit at any moment; there is no Cancel for uploads,
   as in MacShot, and quitting waits for running uploads (`macshot/Upload/PreparedUploadBody.swift:55-76`).
7. **Success.** The link is copied to the clipboard as plain text and the toast shows it (imgbb also shows the delete
   link) (`macshot/AppDelegate.swift:2202-2244`). The copy is a background copy through `platform::ClipboardService`,
   because the overlay is usually gone by then ([08](08-platform-integration.md)). An imgbb upload appends
   `{link, deleteURL}` to the local upload history (`imgbbUploads`), shown in the Uploads tab and never exported
   ([07 §3](07-storage-history-settings.md)).
8. **Failure.** The toast shows the provider's error message; nothing is retried except where the provider section
   says so.

```cpp
namespace ariadshot::net {

class Uploader {
public:
    enum class Kind { Image, Video };
    virtual bool supports(Kind) const = 0;
    virtual ConfigurationState configuration() const = 0;   // Ready | NotConfigured | SignedOut
    // Runs as an export-coordinator job; progress in [0, 1] on the GUI thread when the provider reports it.
    virtual void upload(StagedBody, QString filename, QString contentType,
                        std::function<void(double)> progress,
                        std::function<void(Expected<UploadResult, UploadError>)> done) = 0;
};

struct UploadResult { QUrl link; std::optional<QUrl> deleteUrl; };

} // namespace ariadshot::net
```

## 3. imgbb

`net::ImgbbUploader` ports `macshot/Upload/ImgbbUploader.swift:9-56` (images only).

- `POST https://api.imgbb.com/1/upload?key=<key>` with `multipart/form-data`, one field `image`, a 60 s timeout.
- Success requires HTTP 2xx, `success == true`, and both `data.url` and `data.delete_url`; otherwise the error is
  `error.message` from the response, or "imgbb returned HTTP <status> or an unreadable response".
- **Key.** A key the user entered in the Uploads tab is used first; it lives in `platform::SecretStore`
  ([11 §4](11-security-privacy.md#4-secrets)). Otherwise AriadShot's own shared key is used when the release build was
  given one through a build option; MacShot's embedded key is never reused (DEV-31). With neither, imgbb is "not
  configured": the pre-flight fails with "imgbb not configured. Check Settings." (an AriadShot string modelled on
  MacShot's S3 message, which has no imgbb counterpart because MacShot always embeds a key) and the Uploads tab asks
  for a key. The Uploads-tab note keeps MacShot's meaning: a shared key may be
  included, a personal free key avoids rate limits, and imgbb takes images only.

## 4. Google Drive

`net::GoogleDriveUploader` ports `macshot/Upload/GoogleDriveUploader.swift` (images and videos).

**Client.** AriadShot uses its own Google Cloud OAuth client of the "Desktop app" type. Its client ID and client secret
are supplied to release builds through build options. Google requires the desktop client secret at the token endpoint
even with PKCE and documents it as non-confidential, so embedding it in the binary protects nothing and hides nothing.
MacShot's client ID and callback scheme are never reused (DEV-31). A build without a client shows Google Drive as
"not configured" and does not offer Sign In.

**Sign-in** (both platforms, one flow):

1. `QOAuth2AuthorizationCodeFlow` (Qt Network Authorization) with PKCE `S256` and a random `state`.
2. A loopback reply handler (`QOAuthHttpServerReplyHandler`) listening on `127.0.0.1` on an ephemeral port, per
   RFC 8252; the redirect URI is `http://127.0.0.1:<port>/`.
3. The authorization URL `https://accounts.google.com/o/oauth2/v2/auth` opens in the default browser through
   `platform::UrlOpener`. Scopes are MacShot's: `https://www.googleapis.com/auth/drive.file` and `email`
   (`macshot/Upload/GoogleDriveUploader.swift:12, 65`), so AriadShot sees only files it created.
4. The reply handler accepts exactly one redirect whose `state` matches, answers with a short "you can close this tab"
   page, and stops listening. Closing Settings or quitting before the redirect arrives cancels the flow and stops the
   listener.
5. The code is exchanged at `https://oauth2.googleapis.com/token`. The refresh token goes to `platform::SecretStore`;
   the access token stays in memory only. The account e-mail is read from
   `https://www.googleapis.com/oauth2/v2/userinfo` and stored as `gdriveUserEmail`, a machine key.

MacShot signs in through `ASWebAuthenticationSession` with a custom URL scheme on macOS. AriadShot uses the loopback
flow on macOS too, so one client and one code path serve both platforms; the user-visible difference is that the
browser shows the consent page in a normal tab.

**Sign-out** removes the refresh token, the e-mail and the cached folder ID, and advances an account generation.
Every upload step checks the generation, so a response that arrives after sign-out fails with "Account changed during
upload" instead of restoring credentials (`macshot/Upload/GoogleDriveUploader.swift:101, 151-154`).

**Folder.** Uploads go to the folder named by `gdriveFolderName`; empty means the default name, which is `AriadShot`
instead of MacShot's `macshot` (brand substitution, DEV-01). The folder is found by name or created through the Drive v3
files API; its ID is cached, and concurrent lookups for the same name share one request.

**Idempotent upload with linear retry** (`macshot/Upload/GoogleDriveUploader.swift:274-317`):

1. `GET https://www.googleapis.com/drive/v3/files/generateIds?count=1&space=drive` reserves a file ID.
2. The body is `multipart/related`: JSON metadata `{id, name, parents: [folder]}` and the file, staged once (§2).
3. `POST https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart`, up to three attempts:
   - 401 → refresh the access token and retry at once;
   - 409 on attempt 2 or 3 → an earlier attempt succeeded with this ID: success;
   - 5xx → wait 2 s × attempt, retry;
   - 404 while the folder ID came from the cache → forget the cached folder, then fail;
   - connection lost, timed out or offline → wait 2 s × attempt, retry;
   - a success whose returned ID differs from the reserved ID → fail with "Upload returned an unexpected file ID".
4. The link is `https://drive.google.com/file/d/<id>/view`.

MIME types: `image/png` for stills, `image/gif` for `.gif`, `video/mp4` otherwise.

## 5. S3-compatible storage

`net::S3Uploader` ports `macshot/Upload/S3Uploader.swift` (images and videos) and works with AWS S3, Cloudflare R2,
MinIO, Backblaze B2 and similar services, with AriadShot's own Signature Version 4 implementation and no SDK.

**Configuration.** Non-secret settings: `s3Endpoint`, `s3Region` (empty means `auto`), `s3Bucket`, `s3PublicURLBase`,
`s3PathPrefix`, `s3PublicRead` (default off). The access key ID and the secret access key live in
`platform::SecretStore` (DEV-33). Every `s3…` key is excluded from settings export by the secret guard
([07 §3](07-storage-history-settings.md)). The uploader is configured when endpoint, bucket, both keys and the
effective region are non-empty (`macshot/Upload/S3Uploader.swift:31-43`).

**Request** (`macshot/Upload/S3Uploader.swift:102-138`):

1. The endpoint must be an `http` or `https` URL with a host and without user information, query or fragment;
   otherwise "Invalid S3 endpoint URL" (`macshot/Upload/S3Uploader.swift:245`). Plain `http` is accepted because MinIO and similar local services often run without
   TLS; the user chose it.
2. The key is the path prefix with `{year}`, `{month}` and `{day}` expanded in local time, a `/` appended when the
   prefix is non-empty and lacks one, then the filename with spaces replaced by `_`
   (`macshot/Upload/S3Uploader.swift:45-58, 109-111`).
3. The URL is path-style, built from the endpoint's parts: `scheme://host[:port]`, then the endpoint's path with
   leading and trailing `/` trimmed (and a `/` prefix when it is non-empty), then `/<bucket>/<key>`; the whole path is
   percent-encoded except `A–Z a–z 0–9 - . _ ~ /` (`macshot/Upload/S3Uploader.swift:117-118`).
4. `PUT` with `Content-Type`, `Host` (with the port when present), `Content-Length`, `x-amz-acl: public-read` when
   public read is on, `x-amz-date`, `x-amz-content-sha256` = the staged body's SHA-256, and the SigV4 `Authorization`
   header over the canonical request with signed headers `content-type;host;x-amz-content-sha256;x-amz-date` (plus
   `x-amz-acl` when present), credential scope `<date>/<region>/s3/aws4_request`.
5. A non-2xx response fails with the `<Message>` of the XML error body, else "HTTP <status>".
6. The link is `s3PublicURLBase` + `/` (only when the base lacks a trailing slash) + the encoded key, or the PUT URL when
   no public base is set.

Video content types: `gif` → `image/gif`, `mp4` → `video/mp4`, `mov` → `video/quicktime`, `webm` → `video/webm`,
otherwise `application/octet-stream` (`macshot/Upload/S3Uploader.swift:88-94`). The signer is tested against AWS's
published Signature Version 4 examples and against MacShot's request-building tests
(`macshotTests/UploadRequestTests.swift`).

## 6. Offline build

The offline build is a compile-time variant, selected by the `ARIADSHOT_OFFLINE` CMake option (build options are
listed in [13](13-build-ci-release.md)). It follows MacShot's `OFFLINE` variant, which removes upload and cloud storage
integrations and keeps update checks (`macshot/Services/BuildVariant.swift:1-9` for the flag, `macshot/UI/Windows/SettingsWindowController.swift:2184-2185` for the stated scope; SH-25).

**Removed from the offline build** (compiled out, not hidden):

- the uploaders, the staged-body and upload-job code, and the Google Drive sign-in;
- the Uploads tab of Settings and its keys, and the upload confirmation key `uploadConfirmEnabled`;
- the Upload buttons of the overlay, editor, floating thumbnail and history panel toolbars and menus, the studio's
  video upload, and the single-key upload action `u` (26 single-key actions instead of 27);
- the upload toast's upload path (the same toast still reports local save and recording errors).

**Kept** in the offline build, as in MacShot:

- **Update checks** (§8): the Check for Updates… item and the automatic and beta settings remain, reading the offline
  variant's own feed (§8, DEV-32).
- **Translation** (§7): MacShot's offline variant does not gate its translation service, so the user-initiated web
  translation stays available.

**Added by AriadShot** (DEV-32): on Linux, on-device model downloads are disabled; the features that need a model offer
"Import model file…" instead, which verifies the catalogue's SHA-256 ([09](09-ml-services.md)).

The About tab shows MacShot's offline note with the product name replaced: "Offline build: upload and cloud storage
integrations are removed. Update checks may still connect to AriadShot's update server. Screenshots and recordings stay
local unless you share or save them yourself." (`macshot/UI/Windows/SettingsWindowController.swift:2184-2191`).

A CI job builds the offline variant and a test asserts that its binaries contain no uploader symbols and that
`NetworkPolicy` refuses `upload`, `accountSignIn` and `modelDownload` ([12](12-testing-strategy.md)).

## 7. Translation endpoint

Translation serves the OCR window's translate header, OCR & Translate, the translate overlay annotation (AN-15) and the
`ocr-translate?target=<code>` action ([08](08-platform-integration.md)).

- **Engine.** `translationProvider` is `google` (default) or `apple` (`macshot/Services/TranslationService.swift:16-23`).
  Apple Translation exists only on macOS 15 and later, through the Swift shim in `backends/macos`
  ([09](09-ml-services.md)). On Linux the Translation section lists the Linux engines instead: the web endpoint and,
  once installed, the offline CTranslate2 engine ([09](09-ml-services.md); DEV-37).
- **Languages.** Thirty target languages, in MacShot's order, from `en` to `vi`
  (`macshot/Services/TranslationService.swift:42-73`); `translateTargetLang` defaults to `en`.
- **Web protocol** (`net::GoogleTranslator`, `macshot/Services/TranslationService.swift:144-246`):
  - one `GET https://translate.googleapis.com/translate_a/single` per non-empty line, with `client=gtx`, `sl=auto`,
    `tl=<target>`, `dt=t` and `q=<text>`, header `User-Agent: Mozilla/5.0`, timeout 10 s;
  - the lines of a batch are requested concurrently; results keep their line positions; a whitespace-only line is
    returned unchanged without a request;
  - the response is a nested JSON array; the translation is the concatenation of the first element of each segment in
    the first array; an unparsable or empty result is an error;
  - the first error fails the whole batch, and the caller shows it (error pill or the OCR window's status).
- **What is sent.** Only the recognised text lines of the region the user chose, to the endpoint above, when the user
  asked for a translation. The endpoint is unofficial: it can rate-limit or change without notice, which surfaces as an
  ordinary translation error.

## 8. Update checks

**macOS** keeps MacShot's Sparkle behaviour (SH-26): a scheduled check every 86,400 s when `SUEnableAutomaticChecks` is
on (default on), no automatic download or install, the beta channel when `betaUpdatesEnabled` is on, and the manual
Check for Updates… item (`macshot/Info.plist:54-63`; `macshot/AppDelegate.swift:317-322, 2489-2503`). AriadShot's
updates are verified with EdDSA signatures only, because macOS builds are ad-hoc signed; the feed, key custody and
installation are specified in [13 §7](13-build-ci-release.md#7-macos-distribution). Sparkle's system profiling stays
off.

**Feeds.** Like MacShot, each build variant has its own appcast: the standard build reads the release appcast and
the offline build reads a separate offline appcast, whose items point at offline-variant releases (MacShot builds its
offline variant with `appcast-offline.xml` as the feed, upstream `.github/workflows/build-release.yml:141,151`). The
feed URLs are build options of the release pipeline ([13 §11](13-build-ci-release.md#11-build-variants)); a variant never reads the other
variant's feed, so an offline user is never offered the standard build.

**Linux** has no self-updater (DEV-36). `net::UpdateFeed` reads the same appcast as the macOS Sparkle build of its
variant and only compares versions:

- Same triggers as macOS: the manual item, and the automatic check at most once per 86,400 s while
  `SUEnableAutomaticChecks` is on. The last check time is a machine key.
- Stable items only, or stable and beta items when `betaUpdatesEnabled` is on; enclosures are ignored.
- If a newer version exists, a notice names the version, links its release notes, and tells the user how to update
  with the package format the build was made for (AUR helper, AppImage download page, Flatpak). AriadShot never
  downloads or replaces its own files on Linux.
- A manual check that finds nothing says so; an automatic check that fails is silent and retries at the next interval.
- The request is a plain `GET` with `User-Agent: AriadShot/<version>`; it carries no identifier.

## 9. Model downloads

`net::ModelDownloader` implements `ml::ModelFetcher` ([02 §12](02-modules-and-interfaces.md)); `app` injects it into
the model store ([09](09-ml-services.md)), so `ml` never links `net` or `Qt6::Network` and every model request passes
this policy as `modelDownload`. The store asks for a download only after consent, only from the HTTPS URL recorded in
the model catalogue, into a partial file in the models directory ([07 §1](07-storage-history-settings.md)). The file becomes
usable only after its SHA-256 matches the catalogue; a mismatch deletes it and shows the failure pill. Cancel deletes
partial data. The offline build has no download path (§6).

## 10. Proxy, TLS, redirects and limits

- **Proxy.** The system proxy configuration is honoured (`QNetworkProxyFactory::setUseSystemConfiguration(true)`),
  including `http_proxy`/`https_proxy` on Linux and the system settings on macOS.
- **TLS.** Certificate verification is never disabled, no certificate is pinned, and no "ignore SSL errors" path exists.
  Plain HTTP is allowed only for a user-configured S3 endpoint with an explicit `http` scheme (§5) and for the OAuth
  loopback redirect on `127.0.0.1` (§4).
- **Redirects.** `QNetworkRequest::NoLessSafeRedirectPolicy`: an HTTPS request never follows a redirect to HTTP.
- **Timeouts.** Translation 10 s, imgbb 60 s; every other request aborts after 60 s without any byte transferred
  (`QNetworkRequest::setTransferTimeout`), which matches the idle timeout MacShot's `URLSession` requests use and never
  aborts a slow but progressing upload.
- **Response limits.** JSON and XML responses (uploads, sign-in, translation, update feed) are read up to 4 MiB; a
  larger response is an error. Model downloads are limited to the catalogue's recorded size. Every response is parsed
  as untrusted input ([11 §2](11-security-privacy.md)).
- **Logging.** Requests are logged by purpose, host and status only; never URLs with query strings (they can carry an
  API key or a translation text), bodies, tokens or headers ([11](11-security-privacy.md)).
