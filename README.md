# baoshan/freedb

Containerized FreeDB HTTP server.

## Step 1: Create

The container expects FreeDB database files be mounted at `/usr/local/cddb`.
It's fine to leave it empty for now, we'll load the database later.

```
mkdir -p ~/Metadata/freedb
docker create --name=freedb -p 8080:8080 -v ~/Metadata/freedb:/usr/local/cddb baoshan/freedb
```

## Step 2: Download

The download script requires `axel` be installed.

+ Execute `./download` to download and extract the latest complete database to `~/Metadata/freedb` (requires hours);
+ Execute `./download http://ftp.freedb.org/pub/freedb/freedb-update-20160601-20160701.tar.bz2` for monthly update.

## Step 3: Index

After step 3, run `docker exec freedb cddbd -fdv` to rebuild the fuzzy match.
