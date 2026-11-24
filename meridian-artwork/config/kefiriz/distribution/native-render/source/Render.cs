using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using SARCExt;
using SwitchThemes.Common;
using SwitchThemes.Common.Bntxx;
using SwitchThemes.Common.Bflyt;
using Syroot.BinaryData;
using Images = SwitchThemes.Common.Images;

internal static class Render
{
    const string HomeId = "0100000000001000";
    const string BntxFile = "timg/__Combined.bntx";
    const string LockFile = "7FADED28CE8756F6D94EBA4671EA97F81F6C0C23000000000000000000000000.ips";
    const string LockSha = "75d4ef28113473774ce02eb72168d0add8298acf2c28fc9d958ef64e4a84d74c";
    const int MaxArchive = 64 * 1024 * 1024;
    static readonly string[] Parts = { "home", "lock", "apps", "set", "news", "user", "psl" };

    static void Check(bool ok, string message)
    {
        if (!ok) throw new InvalidDataException(message);
    }

    static bool IsTag(byte[] data, int pos, string tag)
    {
        return pos >= 0 && pos <= data.Length - tag.Length && Encoding.ASCII.GetString(data, pos, tag.Length) == tag;
    }

    static void NoLinks(string path)
    {
        string current = Path.GetFullPath(path);
        while (current != null)
        {
            if (File.Exists(current) || Directory.Exists(current))
                Check((File.GetAttributes(current) & FileAttributes.ReparsePoint) == 0, "Linked path: " + current);
            current = Path.GetDirectoryName(current);
        }
    }

    static byte[] ReadLimited(string path, int min, int max)
    {
        NoLinks(path);
        using (var file = File.OpenRead(path))
        {
            Check(file.Length >= min && file.Length <= max, "Unexpected file length: " + Path.GetFileName(path));
            using (var reader = new BinaryReader(file))
            {
                int length = (int)file.Length;
                var data = reader.ReadBytes(length);
                Check(data.Length == length, "Incomplete read: " + Path.GetFileName(path));
                return data;
            }
        }
    }

    static SarcData OpenArchive(byte[] data)
    {
        Check(data.Length >= 16 && IsTag(data, 0, "Yaz0"), "Expected a Yaz0 SZS.");
        uint rawSize = (uint)(data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7]);
        Check(rawSize >= 48 && rawSize <= MaxArchive, "Unsupported uncompressed resource size.");
        var raw = ManagedYaz0.Decompress(data);
        Check(IsTag(raw,0,"SARC") && raw[6] == 0xff && raw[7] == 0xfe &&
              BitConverter.ToUInt16(raw,4) == 20 && BitConverter.ToUInt32(raw,8) == raw.Length &&
              IsTag(raw,20,"SFAT"), "Invalid Nintendo Switch graphics archive.");
        int start = BitConverter.ToInt32(raw,12), count = BitConverter.ToUInt16(raw,26);
        int nameTable = 32 + 16*count;
        Check(count > 0 && count <= 8192 && IsTag(raw,nameTable,"SFNT") &&
              start >= nameTable + 8 && start < raw.Length, "Invalid graphics archive directory.");
        var archive = new SarcData();
        var entries = new List<Tuple<int,int,string>>();
        for (int i=0; i<count; ++i)
        {
            int off = 32 + 16*i;
            int name = BitConverter.ToInt32(raw,off+4), lo = BitConverter.ToInt32(raw,off+8), hi = BitConverter.ToInt32(raw,off+12);
            int namePos = nameTable+8+4*(name & 0xffffff);
            Check((name >> 24) == 1 && namePos < start &&
                  lo >= 0 && hi >= lo && hi <= raw.Length-start, "Invalid graphics archive entry.");
            int end = Array.IndexOf(raw,(byte)0,namePos,Math.Min(start-namePos,1024));
            Check(end > namePos, "Invalid graphics resource name.");
            string key = Encoding.ASCII.GetString(raw,namePos,end-namePos);
            uint hash = 0;
            for (int n=namePos; n<end; ++n) hash = unchecked(hash*101+raw[n]);
            Check(hash == BitConverter.ToUInt32(raw,off), "Graphics archive name table does not match the directory.");
            entries.Add(Tuple.Create(lo,hi,key));
        }
        int previousEnd = 0;
        foreach (var entry in entries.OrderBy(x => x.Item1).ThenBy(x => x.Item2))
        {
            Check(entry.Item1 >= previousEnd, "Overlapping graphics archive entries.");
            previousEnd = entry.Item2;
            var content = new byte[entry.Item2-entry.Item1];
            Buffer.BlockCopy(raw,start+entry.Item1,content,0,content.Length);
            archive.Files.Add(entry.Item3,content);
        }
        return archive;
    }

    static byte[] ReadTexture(string root, string name, int w, int h, bool icon)
    {
        int size = 128 + (w*h)/(icon ? 1 : 2);
        var data = ReadLimited(Path.Combine(root,name+".dds"),size,size);
        Check(IsTag(data,0,"DDS ") && IsTag(data,84,icon ? "DXT5" : "DXT1") &&
              BitConverter.ToInt32(data,12)==h && BitConverter.ToInt32(data,16)==w, "Invalid packaged texture " + name);
        return data;
    }

    static void CheckTexture(QuickBntx output, string name, byte[] dds, uint channels)
    {
        var image = new Images.DDS(dds);
        var expected = DDSEncoder.EncodeTex(image);
        var texture = output.FindTex(name);
        Check(texture != null && texture.Width == image.Width && texture.Height == image.Height &&
              texture.Format == (uint)expected.Encoding.formatCode && texture.ChannelTypes == (int)channels &&
              texture.Data.SequenceEqual(expected.Data), "Texture verification failed: " + name);
    }

    static void BindSystemColors(SarcData archive, PatchTemplate template)
    {
        var layout = new BflytFile(archive.Files[template.MainLayoutName]);
        var picture = layout[template.PatchIdentifier] as Pic1Pane;
        Check(picture != null && picture.UserData == null, "Expected a new background picture in " + template.MainLayoutName);
        // The section reader expects the size (12), property count (0) and padding.
        using (var stream = new MemoryStream(new byte[] {12, 0, 0, 0, 0, 0, 0, 0}))
        using (var reader = new BinaryDataReader(stream))
            picture.UserData = new Usd1Pane(reader);
        foreach (var binding in new[] { Tuple.Create("C_W", "1"), Tuple.Create("C_B", "9") })
        {
            picture.UserData.Properties.Add(new Usd1Pane.EditableProperty {
                Name = binding.Item1, type = Usd1Pane.EditableProperty.ValueType.int32,
                value = new[] {binding.Item2, "0", "0", "0"}
            });
        }
        var saved = layout.SaveFile();
        var reloaded = new BflytFile(saved)[template.PatchIdentifier].UserData;
        Check(reloaded != null && reloaded.FindName("C_W").value.SequenceEqual(new[] {"1","0","0","0"}) &&
              reloaded.FindName("C_B").value.SequenceEqual(new[] {"9","0","0","0"}),
              "Could not persist native palette bindings.");
        archive.Files[template.MainLayoutName] = saved;
    }

    static byte[] AuroraStroke(Pic1Pane surface, ByteOrder order, string name, ushort material,
                               float x, float y, float width, float height, float angle, RGBAColor tip,
                               byte opacity = 94, byte leftAlpha = 0)
    {
        // Preserve the surface's supported UV format. Only this new picture uses the accent material.
        var pane = new Pic1Pane((byte[])surface.data.Clone(), order) {
            PaneName = name, MaterialIndex = material, Visible = true, InfluenceAlpha = true,
            originX = Pan1Pane.OriginX.Center, originY = Pan1Pane.OriginY.Center,
            ParentOriginX = Pan1Pane.OriginX.Center, ParentOriginY = Pan1Pane.OriginY.Center,
            Position = new Vector3(x, y, 0), Rotation = new Vector3(0, 0, angle),
            Scale = new Vector2(1, 1), Size = new Vector2(width, height), Alpha = opacity,
            ColorTopRight = tip, ColorBottomRight = tip,
            ColorTopLeft = new RGBAColor(tip.R, tip.G, tip.B, leftAlpha),
            ColorBottomLeft = new RGBAColor(tip.R, tip.G, tip.B, leftAlpha)
        };
        using (var bytes = new MemoryStream())
        using (var writer = new BinaryDataWriter(bytes)) {
            writer.ByteOrder = order;
            pane.WritePane(writer);
            return bytes.ToArray();
        }
    }

    static byte[] AddAuroraEdges(byte[] stock, BflytFile layout, PatchTemplate template)
    {
        var surface = layout[template.targetPanels.Single()] as Pic1Pane;
        Check(surface != null && surface.Parent != null && layout.FileByteOrder == ByteOrder.LittleEndian &&
              layout.Mat1 != null && surface.MaterialIndex < layout.Mat1.Materials.Count,
              "Unsupported native surface in " + template.MainLayoutName);
        var materials = layout.Mat1.Materials;
        Check(materials.Count < ushort.MaxValue && materials.All(m => m.Name != "KEFIRIZ_Aurora"),
              "Export the original screen again: " + template.MainLayoutName);
        var material = new BflytMaterial(materials[surface.MaterialIndex].GetData(), layout.FileByteOrder, layout.Version) {
            Name = "KEFIRIZ_Aurora", ForegroundColor = new RGBAColor(255, 255, 255),
            BackgroundColor = new RGBAColor(255, 255, 255)
        };
        var accent = material.Write(layout.Version, layout.FileByteOrder);
        var pearl = new RGBAColor(250, 244, 231);
        var coral = new RGBAColor(235, 99, 76);
        var graphite = new RGBAColor(45, 48, 50);
        ushort index = (ushort)materials.Count;
        var strokes = new List<byte[]>();
        void Layer(string name, float x, float y, float w, float h, RGBAColor color,
                   byte alpha, byte left = 255, float angle = 0) =>
            strokes.Add(AuroraStroke(surface, layout.FileByteOrder, "KEFIRIZ_" + name, index,
                x, y, w, h, angle, color, alpha, left));
        if (template.NXThemeName == "home") {
            Layer("Header", 0, 303, 1280, 114, graphite, 23, 55);
            Layer("Gallery", 0, 51, 1280, 358, graphite, 18, 26);
            Layer("GalleryLine", 0, 230, 1280, 2, graphite, 84, 8);
            Layer("GalleryEnd", -558, -129, 100, 3, coral, 210);
            Layer("DockWash", 0, -202, 1280, 116, graphite, 26, 50);
            Layer("DockA", -251, -197, 396, 89, graphite, 52, 120);
            Layer("DockB", 74, -197, 200, 89, graphite, 52, 120);
            Layer("DockC", 353, -197, 298, 89, graphite, 52, 120);
            Layer("Badge", -531, -197, 92, 89, graphite, 255);
            Layer("UpperFold", -529, -189, 31, 7, pearl, 255, 255, 34);
            Layer("LowerFold", -529, -205, 31, 7, pearl, 255, 255, -34);
            Layer("FoldTip", -520, -183, 9, 7, coral, 255, 255, 34);
        } else {
            Layer("Header", 0, 305, 1280, 110, graphite, 29, 60);
            Layer("Spine", -638, 0, 4, 720, graphite, 110);
            Layer("Margin", -577, 0, 126, 720, graphite, 26, 0, 180);
            Layer("HeaderRule", -530, 249, 154, 3, coral, 205);
            Layer("Footer", 0, -334, 1280, 52, graphite, 19, 16);
        }
        // Keep the fold inside the footer. These pictures have no input or animation bindings.
        Layer("FooterFold", 572, -325, 191, 13, graphite, 97, 10, 24);
        Layer("FooterSeam", 583, -322, 115, 2, coral, 180, 0, 24);
        // Splice whole FLYT sections so the original pane bytes, animation targets and materials survive verbatim.
        using (var output = new MemoryStream())
        using (var writer = new BinaryWriter(output))
        {
            int sections = BitConverter.ToUInt16(stock, 16), pos = 20;
            bool inserted = false, appended = false;
            writer.Write(stock, 0, pos);
            for (int i = 0; i < sections; ++i) {
                Check(pos <= stock.Length - 8, "Truncated layout section.");
                int size = BitConverter.ToInt32(stock, pos + 4);
                Check(size >= 8 && size <= stock.Length - pos, "Invalid layout section length.");
                if (IsTag(stock, pos, "mat1")) {
                    Check(!appended && BitConverter.ToUInt16(stock, pos + 8) == index &&
                          size >= 12 + 4 * index, "Invalid native material directory.");
                    writer.Write(stock, pos, 4);
                    writer.Write(checked(size + 4 + accent.Length));
                    writer.Write((ushort)(index + 1));
                    writer.Write(stock, pos + 10, 2);
                    for (int m = 0; m < index; m++)
                        writer.Write(checked(BitConverter.ToInt32(stock, pos + 12 + 4 * m) + 4));
                    writer.Write(size + 4);
                    writer.Write(stock, pos + 12 + 4 * index, size - 12 - 4 * index);
                    writer.Write(accent);
                    appended = true;
                } else {
                    writer.Write(stock, pos, size);
                }
                bool target = IsTag(stock, pos, "pic1") && size >= 36 &&
                    Encoding.ASCII.GetString(stock, pos + 12, 24).TrimEnd('\0') == surface.PaneName;
                pos += size;
                if (!target) continue;
                Check(!inserted, "Ambiguous native background.");
                // User data belongs to the preceding pane. Keep native palette data attached to the stock background.
                if (i + 1 < sections && IsTag(stock, pos, "usd1")) {
                    int usdSize = BitConverter.ToInt32(stock, pos + 4);
                    Check(usdSize >= 8 && usdSize <= stock.Length - pos, "Invalid native palette section.");
                    writer.Write(stock, pos, usdSize);
                    pos += usdSize;
                    i++;
                }
                // A background may own children. Its accents belong above that subtree, as siblings.
                if (i + 1 < sections && IsTag(stock, pos, "pas1")) {
                    int depth = 0;
                    do {
                        Check(++i < sections && pos <= stock.Length - 8, "Unterminated background subtree.");
                        int childSize = BitConverter.ToInt32(stock, pos + 4);
                        Check(childSize >= 8 && childSize <= stock.Length - pos, "Invalid background child.");
                        if (IsTag(stock, pos, "pas1")) depth++;
                        if (IsTag(stock, pos, "pae1")) depth--;
                        writer.Write(stock, pos, childSize);
                        pos += childSize;
                    } while (depth > 0);
                }
                foreach (var stroke in strokes) writer.Write(stroke);
                inserted = true;
            }
            Check(inserted && appended, "Native artwork could not be attached.");
            writer.Write(stock, pos, stock.Length - pos);
            output.Position = 12;
            writer.Write(checked((int)output.Length));
            writer.Write(checked((ushort)(sections + strokes.Count)));
            var result = output.ToArray();
            var reread = new BflytFile(result);
            var verified = (Pic1Pane)reread[surface.PaneName];
            Check(verified.data.SequenceEqual(surface.data) &&
                  reread.Mat1.Materials.Count == index + 1 &&
                  materials.Select((m, n) => reread.Mat1.Materials[n].GetData().SequenceEqual(m.GetData())).All(x => x) &&
                  ((Pic1Pane)reread["KEFIRIZ_Header"]).MaterialIndex == index,
                  "Native surface or material changed during verification.");
            return result;
        }
    }

    static byte[] BuildPart(string basePath, string art, string part, string title, bool nativeEdges)
    {
        var archive = OpenArchive(ReadLimited(basePath, 16, MaxArchive));
        var p = new SzsPatcher(archive);
        string filename = Path.GetFileName(basePath);
        Check(p.PatchTemplate != null && p.PatchTemplate.NXThemeName == part &&
              p.PatchTemplate.szsName == filename && p.PatchTemplate.TitleId == title,
              "The cached file is not the expected screen: " + filename);
        if (part == "home") Check(p.TargetFirmware == ConsoleFirmware.Fw20_0, "The cached HOME layout is not the 20.x family.");
        if (part == "lock") Check(p.TargetFirmware == ConsoleFirmware.Fw9_0 && p.PatchTemplate.RequiresCodePatch,
              "Unexpected lock-screen template.");
        var layout = new BflytFile(archive.Files[p.PatchTemplate.MainLayoutName]);
        Check(layout[p.PatchTemplate.PatchIdentifier] == null && layout["3x3lxBG"] == null,
              "Export this screen again from the console. It already contains a theme: " + filename);
        var original = new QuickBntx(archive.Files[BntxFile]);
        byte[] bg = null;
        if (nativeEdges) {
            archive.Files[p.PatchTemplate.MainLayoutName] = AddAuroraEdges(archive.Files[p.PatchTemplate.MainLayoutName], layout, p.PatchTemplate);
            if (part == "home") NativeHomeStyle.Arrange(archive);
            else NativeHomeStyle.StyleApplet(archive, part);
        } else {
            bg = ReadTexture(art,"image",1280,720,false);
            Check(p.PatchMainBG(bg), "Background does not fit " + filename);
        }
        var icons = new Dictionary<string,Tuple<byte[],uint>>();
        List<TextureReplacement> replacements;
        if (TextureReplacement.NxNameToList.TryGetValue(part,out replacements))
        {
            foreach (var icon in replacements)
            {
                var target = icon.BntxNames.FirstOrDefault(name => original.FindTex(name) != null);
                Check(target != null && p.TargetFirmware >= icon.MinFirmware, "Expected original icon: " + icon.NxThemeName);
                var dds = ReadTexture(art,icon.NxThemeName,icon.W,icon.H,true);
                var stockPane = nativeEdges ? new BflytFile(archive.Files[icon.FileName])[icon.PaneName] : null;
                Check(p.PatchAppletIcon(dds,icon.NxThemeName), "Unsupported icon layout: " + icon.NxThemeName);
                if (nativeEdges) {
                    // The upstream full-color icon preset forces a grey tint. Our white alpha masks use Horizon's
                    // existing per-button color controllers, including the selected and unselected states.
                    Check(stockPane != null, "Missing native icon pane: " + icon.NxThemeName);
                    var buttons = new BflytFile(archive.Files[icon.FileName]);
                    buttons[icon.PaneName].UserData = stockPane.UserData;
                    archive.Files[icon.FileName] = buttons.SaveFile();
                    NativeHomeStyle.ShapeDockButton(archive.Files[icon.FileName]);
                }
                icons.Add(target,Tuple.Create(dds,icon.NewColorFlags));
            }
        }
        var updated = p.GetFinalSarc();
        if (!nativeEdges) BindSystemColors(updated, p.PatchTemplate);
        var result = SARC.Pack(updated);
        var packed = ManagedYaz0.Compress(result.Item2,3,result.Item1);
        var checkedArchive = OpenArchive(packed);
        Check(checkedArchive.Files.Count == updated.Files.Count &&
              updated.Files.All(entry => checkedArchive.Files[entry.Key].SequenceEqual(entry.Value)),
              "Graphics archive did not round-trip: " + filename);
        var textures = new QuickBntx(checkedArchive.Files[BntxFile]);
        Check(textures.Textures.Select(t=>t.Name).SequenceEqual(original.Textures.Select(t=>t.Name)),
              "The screen texture directory changed: " + filename);
        foreach (var texture in original.Textures)
            if ((nativeEdges || texture.Name != p.PatchTemplate.MaintextureName) && !icons.ContainsKey(texture.Name))
                Check(textures.FindTex(texture.Name).Data.SequenceEqual(texture.Data),
                      "An unrelated texture changed: " + texture.Name);
        if (!nativeEdges) CheckTexture(textures,p.PatchTemplate.MaintextureName,bg,0x05040302);
        foreach (var item in icons) CheckTexture(textures,item.Key,item.Value.Item1,item.Value.Item2);
        return packed;
    }

    static bool LockReady(string folder, string input)
    {
        if (folder == null) return false;
        var cache = new DirectoryInfo(input);
        if (cache.Name.Equals("extracted",StringComparison.OrdinalIgnoreCase)) cache = cache.Parent;
        if (cache.Parent == null || cache.Parent.Parent == null ||
            !cache.Name.Equals("systemData",StringComparison.OrdinalIgnoreCase) ||
            !cache.Parent.Name.Equals("themes",StringComparison.OrdinalIgnoreCase)) return false;
        string active = Path.Combine(cache.Parent.Parent.FullName,"atmosphere","exefs_patches");
        string supplied = new DirectoryInfo(folder).FullName.TrimEnd(Path.DirectorySeparatorChar);
        if (!active.Equals(supplied,StringComparison.OrdinalIgnoreCase) &&
            !Path.Combine(active,"NxThemesInstaller").Equals(supplied,StringComparison.OrdinalIgnoreCase)) return false;
        NoLinks(active);
        if (!Directory.Exists(active)) return false;
        string id = Path.GetFileNameWithoutExtension(LockFile);
        foreach (var set in new DirectoryInfo(active).EnumerateDirectories())
        {
            if ((set.Attributes & FileAttributes.ReparsePoint) != 0) continue;
            // Atmosphere also accepts names with trailing zero bytes omitted.
            for (string name=id; ; name=name.Substring(0,name.Length-2))
            {
                string path = Path.Combine(set.FullName,name+".ips");
                if (File.Exists(path) && new FileInfo(path).Length == 19)
                {
                    using (var sha = SHA256.Create())
                        if (BitConverter.ToString(sha.ComputeHash(ReadLimited(path,19,19))).Replace("-","").ToLowerInvariant() == LockSha)
                            return true;
                }
                if (!name.EndsWith("00",StringComparison.Ordinal)) break;
            }
        }
        return false;
    }

    static int Run(string[] args)
    {
        var options = new Dictionary<string,string>();
        var allowed = new[] {"--base","--output","--palette","--lock-support","--stock-lock","--list","--newer-than"};
        Check(args.Length % 2 == 0, "Expected named argument/value pairs.");
        for (int i=0; i<args.Length; i+=2)
        {
            Check(allowed.Contains(args[i]), "Unknown option " + args[i]);
            options.Add(args[i],args[i+1]);
        }
        string input = Path.GetFullPath(options["--base"]), output = Path.GetFullPath(options["--output"]);
        string palette = options["--palette"];
        Check(palette == "Light" || palette == "Dark", "Expected Dark or Light.");
        NoLinks(input);
        NoLinks(output);
        bool modern = new DirectoryInfo(input).Name.Equals("extracted",StringComparison.OrdinalIgnoreCase);
        string SourceFor(string part)
        {
            string name = Info.PartToFileName[part];
            if (!modern) return Path.Combine(input,name);
            string title = part == "psl" ? "playerselect" : part == "user" ? "mypage" : "qlaunch";
            return Path.Combine(input,title,"lyt",name);
        }
        string marker = Path.Combine(input,"ver.cfg");
        if (!File.Exists(marker) || !File.Exists(SourceFor("home")) ||
            Encoding.ASCII.GetString(ReadLimited(marker,1,64)).TrimEnd('\r','\n') != "20.5.0")
        {
            Console.Error.WriteLine("This card has no matching exported 20.5.0 HOME resources.");
            return 2;
        }
        Check(Directory.Exists(output) && !Directory.EnumerateFileSystemEntries(output).Any(),
              "Supply an empty PC output directory.");
        string support;
        options.TryGetValue("--lock-support",out support);
        bool stockLock = options.ContainsKey("--stock-lock");
        if (stockLock) Check(options["--stock-lock"] == "yes","Expected --stock-lock yes.");
        // A single light-toned motif supplies the texture weights for both system color sets.
        string assets = Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"assets","Light");
        bool listOnly = options.ContainsKey("--list") && options["--list"] == "yes";
        string hold;
        DateTime? after = null;
        if (options.TryGetValue("--newer-than",out hold))
        {
            NoLinks(hold);
            Check(File.Exists(hold), "Recovery marker disappeared.");
            after = File.GetLastWriteTimeUtc(hold);
            Check(File.GetLastWriteTimeUtc(marker) > after.Value, "Export the layouts again after recovery.");
        }
        var files = new Dictionary<string,byte[]>();
        foreach (string part in Parts)
        {
            if (part == "lock" && stockLock) continue;
            string name = Info.PartToFileName[part], path = SourceFor(part);
            if (part == "home") Check(File.Exists(path), "The HOME cache disappeared during preparation.");
            if (!File.Exists(path))
            {
                Console.Error.WriteLine("Missing exported screen: " + name);
                continue;
            }
            if (part == "lock" && !LockReady(support,input))
            {
                Console.Error.WriteLine("Lock Screen: the matching NXThemes compatibility component is not active on this card.");
                continue;
            }
            Check(!after.HasValue || File.GetLastWriteTimeUtc(path) > after.Value,
                  "Export the layouts again after recovery: " + name);
            string title = part == "psl" ? "0100000000001007" : part == "user" ? "0100000000001013" : HomeId;
            string resource = "atmosphere/contents/"+title+"/romfs/lyt/"+name;
            string art = stockLock && part == "home"
                ? Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "assets", "Meridian", "home")
                : Path.Combine(assets, part);
            var built = BuildPart(path,art,part,title,stockLock);
            if (!listOnly) files.Add(resource,built);
            else Console.WriteLine(resource);
        }
        if (listOnly) return 0;
        string stage = Path.Combine(output,".render-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        try
        {
            foreach (var item in files)
            {
                string path = Path.Combine(stage,item.Key);
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                File.WriteAllBytes(path,item.Value);
            }
            Directory.Move(Path.Combine(stage,"atmosphere"),Path.Combine(output,"atmosphere"));
        }
        finally
        {
            // The host owns this private staging directory and all of its outputs.
            if (Path.GetDirectoryName(stage) == output && Directory.Exists(stage))
                Directory.Delete(stage,true);
        }
        foreach (string path in files.Keys) Console.WriteLine(path);
        return 0;
    }

    static int Main(string[] args)
    {
        try { return Run(args); }
        catch (Exception ex)
        {
            Console.Error.WriteLine("Native theme preparation failed: " + ex.Message);
            return 1;
        }
    }
}
