// Helper a Unity mod's before_launch hook runs on the device.
//   interopgen <GameAssembly.dll> <global-metadata.dat> <unity version> <unity-libs dir> <out dir>
//     Il2CppInterop assemblies for an IL2CPP game, as BepInEx 6 generates at first launch.
//   interopgen monoinject <UnityEngine.CoreModule.dll> <out file> [boot assembly name]
//     A copy whose UnityEngine.Application static constructor loads the boot assembly.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using AssetRipper.Primitives;
using Cpp2IL.Core;
using Cpp2IL.Core.Api;
using Cpp2IL.Core.InstructionSets;
using Cpp2IL.Core.OutputFormats;
using Cpp2IL.Core.ProcessingLayers;
using Il2CppInterop.Common;
using Il2CppInterop.Generator;
using Il2CppInterop.Generator.Runners;
using LibCpp2IL;
using Microsoft.Extensions.Logging;

class Program
{
    static int MonoInject(string[] a)
    {
        string input = a[1], output = a[2], boot = a.Length > 3 ? a[3] : "ArmadaSecondScreenBoot";
        var resolver = new Mono.Cecil.DefaultAssemblyResolver();
        resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(input)));
        using var asm = Mono.Cecil.AssemblyDefinition.ReadAssembly(input, new Mono.Cecil.ReaderParameters { AssemblyResolver = resolver, InMemory = true });
        var module = asm.MainModule;
        var app = module.GetType("UnityEngine.Application") ?? throw new Exception("UnityEngine.Application not found");
        var ts = module.TypeSystem;
        var cctor = app.Methods.FirstOrDefault(m => m.IsConstructor && m.IsStatic);
        if (cctor == null)
        {
            cctor = new Mono.Cecil.MethodDefinition(".cctor", Mono.Cecil.MethodAttributes.Private | Mono.Cecil.MethodAttributes.Static |
                Mono.Cecil.MethodAttributes.HideBySig | Mono.Cecil.MethodAttributes.SpecialName | Mono.Cecil.MethodAttributes.RTSpecialName, ts.Void);
            cctor.Body.GetILProcessor().Emit(Mono.Cecil.Cil.OpCodes.Ret);
            app.Methods.Add(cctor);
        }
        // Without this the runtime may defer the constructor until a static field is touched.
        app.IsBeforeFieldInit = false;
        // references are built against the game's own core library, not this tool's
        var scope = ts.CoreLibrary;
        Mono.Cecil.TypeReference T(string ns, string name) => new Mono.Cecil.TypeReference(ns, name, module, scope);
        var tAssembly = T("System.Reflection", "Assembly"); var tType = T("System", "Type");
        var tMethodInfo = T("System.Reflection", "MethodInfo"); var tMethodBase = T("System.Reflection", "MethodBase"); var tException = T("System", "Exception");
        var load = new Mono.Cecil.MethodReference("Load", tAssembly, tAssembly); load.Parameters.Add(new Mono.Cecil.ParameterDefinition(ts.String));
        var getType = new Mono.Cecil.MethodReference("GetType", tType, tAssembly) { HasThis = true }; getType.Parameters.Add(new Mono.Cecil.ParameterDefinition(ts.String));
        var getMethod = new Mono.Cecil.MethodReference("GetMethod", tMethodInfo, tType) { HasThis = true }; getMethod.Parameters.Add(new Mono.Cecil.ParameterDefinition(ts.String));
        var invoke = new Mono.Cecil.MethodReference("Invoke", ts.Object, tMethodBase) { HasThis = true };
        invoke.Parameters.Add(new Mono.Cecil.ParameterDefinition(ts.Object)); invoke.Parameters.Add(new Mono.Cecil.ParameterDefinition(new Mono.Cecil.ArrayType(ts.Object)));

        var il = cctor.Body.GetILProcessor();
        var first = cctor.Body.Instructions[0];
        var tryStart = Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Ldstr, boot);
        var handler = Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Pop);
        var seq = new[]
        {
            tryStart,
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Call, load),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Ldstr, "ArmadaSecondScreen.Boot"),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Callvirt, getType),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Ldstr, "Start"),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Callvirt, getMethod),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Ldnull),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Ldnull),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Callvirt, invoke),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Pop),
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Leave, first),
            handler,
            Mono.Cecil.Cil.Instruction.Create(Mono.Cecil.Cil.OpCodes.Leave, first),
        };
        foreach (var ins in seq) il.InsertBefore(first, ins);
        cctor.Body.ExceptionHandlers.Add(new Mono.Cecil.Cil.ExceptionHandler(Mono.Cecil.Cil.ExceptionHandlerType.Catch)
        {
            TryStart = tryStart, TryEnd = handler, HandlerStart = handler, HandlerEnd = first, CatchType = tException
        });
        cctor.Body.InitLocals = true;
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output)));
        asm.Write(output + ".tmp");
        File.Move(output + ".tmp", output, true);
        return 0;
    }

    static int Main(string[] a)
    {
        if (a.Length > 0 && a[0] == "monoinject") return MonoInject(a);
        string gameAssembly = a[0], metadata = a[1], unityLibs = a[3], outDir = a[4];
        var version = UnityVersion.Parse(a[2]);
        Cpp2IL.Core.Logging.Logger.InfoLog += (m, s) => Console.WriteLine("[" + s + "] " + m.Trim());
        Cpp2IL.Core.Logging.Logger.WarningLog += (m, s) => Console.WriteLine("[W " + s + "] " + m.Trim());
        Cpp2IL.Core.Logging.Logger.ErrorLog += (m, s) => Console.WriteLine("[E " + s + "] " + m.Trim());
        InstructionSetRegistry.RegisterInstructionSet<X86InstructionSet>(DefaultInstructionSets.X86_32);
        InstructionSetRegistry.RegisterInstructionSet<X86InstructionSet>(DefaultInstructionSets.X86_64);
        LibCpp2IlBinaryRegistry.RegisterBuiltInBinarySupport();
        Cpp2IlApi.InitializeLibCpp2Il(gameAssembly, metadata, version);
        var layers = new List<Cpp2IlProcessingLayer> { new AttributeInjectorProcessingLayer() };
        foreach (var l in layers) l.PreProcess(Cpp2IlApi.CurrentAppContext, layers);
        foreach (var l in layers) l.Process(Cpp2IlApi.CurrentAppContext);
        var assemblies = new AsmResolverDllOutputFormatDefault().BuildAssemblies(Cpp2IlApi.CurrentAppContext);
        LibCpp2IlMain.Reset();
        Cpp2IlApi.CurrentAppContext = null;
        Console.WriteLine("cpp2il done, " + assemblies.Count + " assemblies");
        Directory.CreateDirectory(outDir);
        var opts = new GeneratorOptions { GameAssemblyPath = gameAssembly, Source = assemblies, OutputDir = outDir, UnityBaseLibsDir = unityLibs };
        using var lf = LoggerFactory.Create(b => b.AddSimpleConsole());
        Il2CppInteropGenerator.Create(opts).AddLogger(lf.CreateLogger("Il2CppInteropGen")).AddInteropAssemblyGenerator().Run();
        Console.WriteLine("interop done");
        return 0;
    }
}
