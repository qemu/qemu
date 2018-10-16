// Replace uses of in-place byteswapping functions with calls to the
// equivalent not-in-place functions.  This is necessary to avoid
// undefined behaviour if the expression being swapped is a field in a
// packed struct.

@@
expression E;
@@
-be16_to_cpus(&E);
+E = be16_to_cpu(E);
@@
expression E;
@@
-be32_to_cpus(&E);
+E = be32_to_cpu(E);
@@
expression E;
@@
-be64_to_cpus(&E);
+E = be64_to_cpu(E);
@@
expression E;
@@
-cpu_to_be16s(&E);
+E = cpu_to_be16(E);
@@
expression E;
@@
-cpu_to_be32s(&E);
+E = cpu_to_be32(E);
@@
expression E;
@@
-cpu_to_be64s(&E);
+E = cpu_to_be64(E);
@@
expression E;
@@
-le16_to_cpus(&E);
+E = le16_to_cpu(E);
@@
expression E;
@@
-le32_to_cpus(&E);
+E = le32_to_cpu(E);
@@
expression E;
@@
-le64_to_cpus(&E);
+E = le64_to_cpu(E);
@@
expression E;
@@
-cpu_to_le16s(&E);
+E = cpu_to_le16(E);
@@
expression E;
@@
-cpu_to_le32s(&E);
+E = cpu_to_le32(E);
@@
expression E;
@@
-cpu_to_le64s(&E);
+E = cpu_to_le64(E);
