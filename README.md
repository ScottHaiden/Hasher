# Hasher
Store and manage file hashes as extended attributes.

Hasher is a simple utility, similar to `sha512sum` or `md5sum`, that will
record or verify the hash of a file or files. But instead of storing them in a
separate hashfile, it stores them as extended attributes on the file itself.
This way, if you rename a file, or copy it to another drive or machine, the
hash follows it.

Also supports hashing or checking in parallel, with multiple threads.
