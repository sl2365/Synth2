import Foundation

/// Where the bank library lives and how picked files reach it. The library
/// is one folder we own; "Import…" COPIES into it (folders copied, zips
/// extracted, loose .sy1 into "Imported": EngineCore/PatchLibrary.hpp
/// ImportItems).
enum SynthLibrary {
    /// iPad: the library shared by the Synth2 app and the AUv3 (app group,
    /// Identity.xcconfig OXO_APP_GROUP via both Info.plists).
    static let appGroup = Bundle.main.object(forInfoDictionaryKey: "OxoAppGroup") as? String ?? ""
    static var appGroupLibraryRoot: URL {
        let base = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: appGroup)
            ?? FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return base.appendingPathComponent("Synth2", isDirectory: true)
    }
    /// iPad: the read-only factory bank in the appex's resources (the mac
    /// VST3 finds its own on the C++ side, EditorCore/Storage.hpp). Its
    /// sibling "Synth2 Starter Banks" is copied into the library once
    /// (PatchLibrary.hpp SeedStarterBanks).
    static var factoryRoot: URL? {
        Bundle.main.resourceURL?.appendingPathComponent("Synth2 Factory", isDirectory: true)
    }

    /// Runs `body` with the picked paths (newline-separated, the import
    /// hook's format) inside their security scopes (iPad file picker).
    static func withAccess<T>(_ urls: [URL], _ body: (String) -> T) -> T {
        let scoped = urls.map { $0.startAccessingSecurityScopedResource() }
        defer { for (url, s) in zip(urls, scoped) where s { url.stopAccessingSecurityScopedResource() } }
        return body(urls.map(\.path).joined(separator: "\n"))
    }
}
