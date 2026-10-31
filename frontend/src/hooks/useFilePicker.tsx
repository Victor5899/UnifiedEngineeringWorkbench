import { useRef } from "react";

export function useFilePicker(accept: string, onFile: (file: File) => void) {
  const inputRef = useRef<HTMLInputElement>(null);
  const open = () => inputRef.current?.click();
  const input = (
    <input
      ref={inputRef}
      className="sr-only"
      type="file"
      accept={accept}
      onChange={(event) => {
        const file = event.target.files?.[0];
        event.currentTarget.value = "";
        if (file) onFile(file);
      }}
    />
  );
  return { open, input };
}
