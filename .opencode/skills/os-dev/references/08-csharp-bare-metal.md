# Referencia — Camino Alternativo: OS en C#

C# no puede ejecutarse directamente sobre metal desnudo porque normalmente depende de un runtime administrado (CLR) con garbage collector, JIT, y llamadas al sistema operativo subyacente para hilos, memoria virtual, etc. Para un OS "en C#" existen dos caminos reales, ambos con trade-offs importantes que hay que explicar claramente al usuario.

## Opción A — Cosmos (C# Open Source Managed Operating System)

Proyecto maduro y específicamente diseñado para esto. Usa **IL2CPU**, un compilador que traduce el IL (Intermediate Language) de .NET directamente a código nativo x86, sin necesitar un runtime completo en tiempo de ejecución.

Flujo de trabajo:
1. Instalar el SDK/plantillas de Cosmos (Visual Studio en Windows es el camino más soportado oficialmente; hay soporte parcial multiplataforma).
2. Crear un proyecto `Kernel` que hereda de `Sys.Kernel` y sobreescribe `BeforeRun()`/`Run()`.
3. Cosmos genera automáticamente el ISO booteable (usa GRUB internamente) — el usuario no escribe el bootloader a mano.
4. Para código de muy bajo nivel (acceso a puertos I/O, `hlt`, `cli`/`sti`) Cosmos expone "plugs": clases en C# que en tiempo de compilación son reemplazadas por IL2CPU con implementaciones que en el fondo son ensamblador. El desarrollador rara vez escribe ASM directamente; lo hace a través de estos plugs o usando los que Cosmos ya provee (`Core.IOPort`, etc.).

```csharp
public class Kernel : Sys.Kernel {
    protected override void BeforeRun() {
        Console.WriteLine("MyOS en C# (Cosmos) iniciando...");
    }
    protected override void Run() {
        Console.Write("myos> ");
        string cmd = Console.ReadLine();
        Console.WriteLine("Comando recibido: " + cmd);
    }
}
```

Trade-offs a comunicar al usuario:
- Mucho menos control de bajo nivel que C/C++/ASM directo; el bootloader, GDT, IDT ya vienen resueltos por el framework, lo cual es bueno para productividad pero malo si el objetivo es *aprender* esas capas.
- El garbage collector de Cosmos es simplificado y puede tener comportamientos distintos al CLR normal; hay que probar exhaustivamente cualquier código que dependa de GC timing.
- Comunidad más pequeña que la de OSDev en C/C++; menos tutoriales para problemas específicos.
- Sigue siendo necesario razonar sobre memoria, interrupciones y hardware — Cosmos reduce el ASM manual, no la complejidad conceptual de un OS.

## Opción B — .NET Native AOT como "kernel shim" sobre un núcleo en C/ASM

Enfoque más experimental y de mayor esfuerzo: usar `dotnet publish -r <rid> -p:PublishAot=true` para compilar C# a código nativo sin runtime JIT, y luego:
1. Escribir el bootloader y la inicialización de bajísimo nivel (GDT, IDT, paginación) en ASM/C exactamente como en las Fases 1–4 de esta skill.
2. Enlazar el binario nativo generado por Native AOT como si fuera una biblioteca más, proveyendo desde C las funciones que el runtime de Native AOT mínimamente necesita (manejo de memoria vía un allocator propio, ya que no hay SO debajo que provea `mmap`/`VirtualAlloc`).
3. Este camino requiere interceptar y reimplementar partes del runtime mínimo de .NET (el "minimal runtime" o CoreRT-like) que normalmente asume la presencia de un sistema operativo. Es territorio de investigación activa, no un tutorial estándar — documenta cualquier limitación encontrada.

Recomienda esta opción solo si el usuario explícitamente quiere experimentar en la frontera, y dilo así: es mucho más frágil y con mucha menos documentación que Cosmos o que C/C++ puro.

## Recomendación por defecto

Si el usuario pide "un sistema operativo en C#" sin más detalle, recomienda **Cosmos** como punto de partida realista, aclarando que igual necesitará entender los conceptos de las Fases 1–7 de esta skill (bootloader, interrupciones, memoria, drivers) aunque estén parcialmente resueltos por el framework — el objetivo educativo de OSDev no desaparece, solo cambia de capa.
