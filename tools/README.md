# Host tooling

- [configurator/](configurator/README.md): localhost profile review, USB deployment
  and telemetry using the shared planner and client.
- [config_validator/](config_validator/README.md): catalogue, schema, board/resource
  and typed-rule admission; it does not activate hardware.
- [config_transfer/](config_transfer/README.md): bounded USB framing, sequence/state
  discovery, compiled capabilities and explicit mutation commands. No automatic
  retry after an uncertain mutation.
- [target/](target/README.md): isolated real target builds with source/artifact
  hashes and compiler-profile checks; building does not flash or qualify a board.
- [c_standard/](c_standard/README.md): mandatory BARR-C checks and audit evidence.

Hardware acceptance is checkpoint 8; AI and model tooling remain checkpoint 9.
The local configurator adapts Member 5's cached design; its README records the
source revision and the retained/replaced pieces.

## Earlier prototype tools

The separate [Member 5 toolkit](../legacy/member5/README.md) retains the original
S3/schema-1.0 CLI, browser UI, verified filesystem bundles and manual evidence
runner. Run it from its own directory. Current C3 deployment uses this folder's
configurator and USB client.
