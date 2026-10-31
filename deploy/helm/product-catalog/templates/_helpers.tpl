{{/*
The component's config: the generated files/product-catalog.json with values `configOverrides` merged over it
(ADR-0513). Rendered into the ConfigMap; its checksum restarts the pod when it changes.
*/}}
{{- define "product-catalog.config" -}}
{{- mustMergeOverwrite (.Files.Get "files/product-catalog.json" | fromJson) (deepCopy .Values.configOverrides) | toPrettyJson -}}
{{- end -}}
