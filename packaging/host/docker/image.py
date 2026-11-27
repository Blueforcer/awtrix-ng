import argparse
import hashlib
import io
import json
import os
import sys
import tarfile
import time
from pathlib import Path

APP = "/opt/awtrix-script-host"
ENTRYPOINT = [f"{APP}/bin/awtrix-host", "--state", "/data", "--binary", f"{APP}/bin/awtrix-script-runner",
              "--webui", f"{APP}/share/index.html", "--ca-file", f"{APP}/share/ca-certificates.crt"]
VARIANTS = {
    "docker": {"user": "999:999", "args": []},
    "ha": {"user": "", "args": ["--name", "Home Assistant", "--ha-ingress"]},
}
HA_ARCH = {"amd64": "amd64", "arm64": "aarch64"}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def digest_of(path):
    sha = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            sha.update(block)
    return sha.hexdigest()


def image_config(arch, variant, version, created, layer):
    labels = {
        "org.opencontainers.image.title": "AWTRIX Script Host",
        "org.opencontainers.image.description": "Runs the scripts of AWTRIX NG clocks.",
        "org.opencontainers.image.source": "https://github.com/Blueforcer/awtrix-ng",
        "org.opencontainers.image.version": version,
        "org.opencontainers.image.licenses": "PolyForm-Noncommercial-1.0.0",
    }
    if variant == "ha":
        labels.update({"io.hass.type": "addon", "io.hass.version": version, "io.hass.arch": HA_ARCH[arch]})
    config = {
        "Entrypoint": ENTRYPOINT + VARIANTS[variant]["args"],
        "ExposedPorts": {"8300/tcp": {}},
        "Labels": labels,
        "StopSignal": "SIGTERM",
        "Volumes": {"/data": {}},
    }
    if VARIANTS[variant]["user"]:
        config["User"] = VARIANTS[variant]["user"]
    image = {
        "architecture": arch,
        "os": "linux",
        "created": created,
        "config": config,
        "rootfs": {"type": "layers", "diff_ids": ["sha256:" + layer]},
        "history": [{"created": created, "created_by": f"packaging/host/docker/image.py {variant}"}],
    }
    if arch == "arm64":
        image["variant"] = "v8"
    return image


def add(archive, name, data=None, path=None, epoch=0):
    info = tarfile.TarInfo(name)
    info.mtime, info.mode, info.uid, info.gid, info.uname, info.gname = epoch, 0o644, 0, 0, "", ""
    if path is not None:
        info.size = os.path.getsize(path)
        with open(path, "rb") as source:
            archive.addfile(info, source)
    else:
        info.size = len(data)
        archive.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description="Writes an image archive for docker load and OCI tools from a "
                                                 "root file system of packaging/host/linux/build.sh.")
    parser.add_argument("--rootfs", required=True, type=Path)
    parser.add_argument("--arch", required=True, choices=sorted(HA_ARCH))
    parser.add_argument("--variant", required=True, choices=sorted(VARIANTS))
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", action="append", default=[])
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    created = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(epoch))
    layer = digest_of(args.rootfs)
    config = canonical(image_config(args.arch, args.variant, args.version, created, layer))
    config_digest = hashlib.sha256(config).hexdigest()
    platform = {"architecture": args.arch, "os": "linux"}
    if args.arch == "arm64":
        platform["variant"] = "v8"
    manifest = canonical({
        "schemaVersion": 2,
        "mediaType": "application/vnd.oci.image.manifest.v1+json",
        "config": {"mediaType": "application/vnd.oci.image.config.v1+json", "digest": "sha256:" + config_digest,
                   "size": len(config)},
        "layers": [{"mediaType": "application/vnd.oci.image.layer.v1.tar", "digest": "sha256:" + layer,
                    "size": os.path.getsize(args.rootfs)}],
    })
    manifest_digest = hashlib.sha256(manifest).hexdigest()
    descriptor = {"mediaType": "application/vnd.oci.image.manifest.v1+json", "digest": "sha256:" + manifest_digest,
                  "size": len(manifest), "platform": platform}
    if args.tag:
        descriptor["annotations"] = {"io.containerd.image.name": args.tag[0],
                                     "org.opencontainers.image.ref.name": args.tag[0].rsplit(":", 1)[-1]}
    index = canonical({"schemaVersion": 2, "mediaType": "application/vnd.oci.image.index.v1+json",
                       "manifests": [descriptor]})
    docker = canonical([{"Config": f"blobs/sha256/{config_digest}", "RepoTags": args.tag,
                         "Layers": [f"blobs/sha256/{layer}"]}])

    args.out.parent.mkdir(parents=True, exist_ok=True)
    partial = args.out.with_name(args.out.name + ".part")
    entries = {
        "blobs/sha256/" + config_digest: config,
        "blobs/sha256/" + manifest_digest: manifest,
        "index.json": index,
        "manifest.json": docker,
        "oci-layout": canonical({"imageLayoutVersion": "1.0.0"}),
    }
    with tarfile.open(partial, "w", format=tarfile.GNU_FORMAT) as archive:
        for name in sorted(list(entries) + ["blobs/sha256/" + layer]):
            if name == "blobs/sha256/" + layer:
                add(archive, name, path=args.rootfs, epoch=epoch)
            else:
                add(archive, name, data=entries[name], epoch=epoch)
    partial.replace(args.out)
    print(f"{args.out.name} sha256:{config_digest} {args.arch} {args.variant} {' '.join(args.tag)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
