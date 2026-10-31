{{/*
The component's config: the generated files/roaming-interconnect.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "roaming-interconnect.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/roaming-interconnect.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
