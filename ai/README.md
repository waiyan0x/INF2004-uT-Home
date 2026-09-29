# Embedded AI — legacy harness and foundation gate

AI work is paused while the reusable platform foundation is reviewed. Follow
[TRAINING_PIPELINE.md](TRAINING_PIPELINE.md) and the
[platform baseline](../PROJECT_BASELINE.md).

The existing `contracts/`, `uthome_ai/`, tests and
`data/sources/first-prototype-baseline.json` retain the historical AI 0.2.0
prototype experiment. That validator intentionally recognizes four original
models and fixed limits. It is not used by the new platform admission layer,
and must not be used to generate new deployment profiles or training data.

The active source of hardware capabilities is `catalogue/`. Generic node
validation lives in `tools/config_validator/`. A future AI contract will resolve
reviewed capabilities, correlate recommendations to instance IDs and use the
same admission path as manually supplied configurations.

Legacy regression checks remain available:

```powershell
python -m pip install -r ai/requirements.txt
python -B -m unittest discover -s ai/tests -v
python -B -m ai.uthome_ai.cli exchange request.json recommendation.json
```

These commands validate the historical experiment; they do not validate node
schema 2.0.0, run training, deploy a model or activate hardware.
