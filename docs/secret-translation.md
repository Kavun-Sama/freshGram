# Secret-chat translation

All message-menu translation entry points use `ShowTranslateBox()`. For a secret
peer it asks for confirmation naming the selected provider before creating the
translation box or making a request. Cancel or dismissal makes no request, and
every invocation asks again.

The boundary covers Telegram, Google, Yandex, custom URL templates and the system
translator. Without a confirmed identity, an unavailable system translator
resolves to Telegram. Changing the
provider or custom URL while confirmation is open requires fresh consent, even
when two custom servers have the same display name. The callback follows the
controller lifetime. Secret whole-chat auto-translation remains disabled.

Provider creation checks the confirmed identity again. If it has changed or the
confirmed system translator cannot be created, translation reports an error
without falling back to a different provider.

Live acceptance checks should verify the warning, Cancel, dismissal, confirmation
and provider/URL changes, as well as ordinary-chat translation. One-off request
spies and private runtime evidence are kept outside Git; screenshots must never
be committed.
