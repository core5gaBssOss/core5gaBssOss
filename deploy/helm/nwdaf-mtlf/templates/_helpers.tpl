{{/*
The NF's config: the generated files/nwdaf-mtlf.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "nwdaf-mtlf.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/nwdaf-mtlf.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
