#import <AppKit/AppKit.h>
#import <ModelIO/ModelIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "Platform/Mac/Platform.hpp"

#include "Engine/Assets/Mesh.hpp"
#include "Engine/Core/Async.hpp"
#include "Engine/Core/Resources.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/Components.hpp"
#include "Localization/Localization.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {

platform::Application* current = nullptr;
int lockDescriptor = -1;

class MainExecutor final : public ld::Executor {
public:
    void post(std::function<void()> job) override {
        auto shared = std::make_shared<std::function<void()>>(std::move(job));
        dispatch_async(dispatch_get_main_queue(), ^{
            (*shared)();
        });
    }
};

MainExecutor& executor() {
    static MainExecutor instance;
    return instance;
}

NSString* text(const std::string& value) { return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding] ?: @""; }

ld::Result<ld::MeshData> importModel(const std::filesystem::path& path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:text(path.string())];
        NSString* extension = url.pathExtension.lowercaseString;
        if (![MDLAsset canImportFileExtension:extension]) return ld::Error::make(ld::ErrorCode::invalidArgument, ld::tr("Formato non supportato"));
        MDLAsset* asset = [[MDLAsset alloc] initWithURL:url];
        NSArray* meshes = [asset childObjectsOfClass:[MDLMesh class]];
        if (meshes.count == 0) return ld::Error::make(ld::ErrorCode::invalidArgument, ld::tr("Nessuna mesh nel file"));
        MDLMesh* mesh = meshes.firstObject;
        [mesh addNormalsWithAttributeNamed:MDLVertexAttributeNormal creaseThreshold:0.5];
        MDLVertexAttributeData* positions = [mesh vertexAttributeDataForAttributeNamed:MDLVertexAttributePosition asFormat:MDLVertexFormatFloat3];
        MDLVertexAttributeData* normals = [mesh vertexAttributeDataForAttributeNamed:MDLVertexAttributeNormal asFormat:MDLVertexFormatFloat3];
        if (!positions) return ld::Error::make(ld::ErrorCode::invalidArgument, ld::tr("Mesh senza vertici"));
        ld::MeshData data;
        for (NSUInteger i = 0; i < mesh.vertexCount; ++i) {
            const float* p = reinterpret_cast<const float*>(static_cast<const char*>(positions.dataStart) + i * positions.stride);
            data.positions.push_back({p[0], p[1], p[2]});
            if (normals) {
                const float* n = reinterpret_cast<const float*>(static_cast<const char*>(normals.dataStart) + i * normals.stride);
                data.normals.push_back({n[0], n[1], n[2]});
            }
        }
        for (MDLSubmesh* submesh in mesh.submeshes) {
            if (submesh.geometryType != MDLGeometryTypeTriangles) continue;
            id<MDLMeshBuffer> buffer = submesh.indexBuffer;
            const char* bytes = static_cast<const char*>(buffer.map.bytes);
            for (NSUInteger i = 0; i < submesh.indexCount; ++i) {
                std::uint32_t index = 0;
                switch (submesh.indexType) {
                case MDLIndexBitDepthUInt8: index = reinterpret_cast<const std::uint8_t*>(bytes)[i]; break;
                case MDLIndexBitDepthUInt16: index = reinterpret_cast<const std::uint16_t*>(bytes)[i]; break;
                default: index = reinterpret_cast<const std::uint32_t*>(bytes)[i]; break;
                }
                if (index < data.positions.size()) data.indices.push_back(index);
            }
        }
        if (data.normals.size() != data.positions.size()) data.computeNormals();
        if (data.empty()) return ld::Error::make(ld::ErrorCode::invalidArgument, ld::tr("Mesh vuota"));
        return data;
    }
}

}

@interface LDMenuTarget : NSObject
@property(nonatomic) NSInteger index;
- (void)perform:(id)sender;
@end

@interface LDApplicationDelegate : NSObject <NSApplicationDelegate>
@end

namespace {
std::vector<std::function<void()>> menuActions;
NSMutableArray* targets;
}

@implementation LDMenuTarget
- (void)perform:(id)sender {
    if (self.index >= 0 && static_cast<std::size_t>(self.index) < menuActions.size() && menuActions[static_cast<std::size_t>(self.index)]) menuActions[static_cast<std::size_t>(self.index)]();
}
@end

@implementation LDApplicationDelegate
- (void)applicationWillFinishLaunching:(NSNotification*)notification {
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:self andSelector:@selector(handleURL:with:) forEventClass:kInternetEventClass andEventID:kAEGetURL];
}
- (void)handleURL:(NSAppleEventDescriptor*)event with:(NSAppleEventDescriptor*)reply {
    NSString* raw = [event paramDescriptorForKeyword:keyDirectObject].stringValue;
    if (!raw || !current || !current->openUrl) return;
    current->openUrl(raw.UTF8String);
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    if (current && current->launched) current->launched();
    [NSApp activateIgnoringOtherApps:YES];
}
- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag {
    if (current && current->reopen) current->reopen();
    return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    if (current && current->quitting && !current->quitting()) return NSTerminateCancel;
    return NSTerminateNow;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}
- (void)applicationWillTerminate:(NSNotification*)notification {
    if (current && current->terminating) current->terminating();
    if (lockDescriptor >= 0) close(lockDescriptor);
}
@end

namespace platform {

void installAdapters() {
    ld::mesh_import::setPlatformImporter(importModel);
    installAudio();
}

int run(Application application) {
    @autoreleasepool {
        static Application stored;
        stored = std::move(application);
        current = &stored;
        ld::setMainQueue(&executor());
        ld::setResourceFolder(bundleResources());
        installAdapters();
        NSApplication* app = [NSApplication sharedApplication];
        LDApplicationDelegate* delegate = [[LDApplicationDelegate alloc] init];
        app.delegate = delegate;
        NSMenu* bar = [[NSMenu alloc] init];
        targets = [NSMutableArray array];
        for (const auto& menu : stored.menus) {
            NSMenuItem* holder = [[NSMenuItem alloc] init];
            NSMenu* submenu = [[NSMenu alloc] initWithTitle:text(menu.title)];
            for (const auto& item : menu.items) {
                NSMenuItem* entry;
                if (!item.selector.empty()) entry = [[NSMenuItem alloc] initWithTitle:text(item.title) action:NSSelectorFromString(text(item.selector)) keyEquivalent:text(item.key)];
                else {
                    LDMenuTarget* target = [[LDMenuTarget alloc] init];
                    target.index = static_cast<NSInteger>(menuActions.size());
                    menuActions.push_back(item.action);
                    [targets addObject:target];
                    entry = [[NSMenuItem alloc] initWithTitle:text(item.title) action:@selector(perform:) keyEquivalent:text(item.key)];
                    entry.target = target;
                }
                [submenu addItem:entry];
            }
            holder.submenu = submenu;
            [bar addItem:holder];
        }
        app.mainMenu = bar;
        [app run];
        return 0;
    }
}

void terminate() { [NSApp terminate:nil]; }

bool sendAction(const std::string& selector) { return [NSApp sendAction:NSSelectorFromString(text(selector)) to:nil from:nil]; }

std::string version() {
    NSString* value = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    return value ? value.UTF8String : LUDISSEY_VERSION;
}

std::string bundleIdentifier() {
    NSString* value = NSBundle.mainBundle.bundleIdentifier;
    return value ? value.UTF8String : "";
}

std::filesystem::path bundleResources() {
    const char* root = std::getenv("LUDISSEY_ROOT");
    NSString* resources = NSBundle.mainBundle.resourcePath;
    if (resources && [[NSFileManager defaultManager] fileExistsAtPath:[resources stringByAppendingPathComponent:@"experience-rules.json"]]) return resources.UTF8String;
    if (root) return std::filesystem::path(root) / "Resources";
    return resources ? std::filesystem::path(resources.UTF8String) : std::filesystem::path("Resources");
}

std::filesystem::path bundlePath() { return NSBundle.mainBundle.bundlePath.UTF8String; }

void openUrl(const std::string& url) {
    NSURL* address = [NSURL URLWithString:text(url)];
    if (address) [[NSWorkspace sharedWorkspace] openURL:address];
}

void openApplication(const std::filesystem::path& bundle, const std::vector<std::string>& urls) {
    NSURL* application = [NSURL fileURLWithPath:text(bundle.string())];
    NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;
    if (urls.empty()) {
        [[NSWorkspace sharedWorkspace] openApplicationAtURL:application configuration:configuration completionHandler:nil];
        return;
    }
    NSMutableArray* list = [NSMutableArray array];
    for (const auto& u : urls)
        if (NSURL* address = [NSURL URLWithString:text(u)]) [list addObject:address];
    [[NSWorkspace sharedWorkspace] openURLs:list withApplicationAtURL:application configuration:configuration completionHandler:nil];
}

std::optional<std::filesystem::path> chooseFile(const std::vector<std::string>& extensions, const std::string& prompt) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.prompt = text(prompt);
    NSMutableArray* types = [NSMutableArray array];
    for (const auto& e : extensions)
        if (UTType* type = [UTType typeWithFilenameExtension:text(e)]) [types addObject:type];
    panel.allowedContentTypes = types;
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return std::nullopt;
    return std::filesystem::path(panel.URL.path.UTF8String);
}

std::optional<std::filesystem::path> chooseDestination(const std::string& name, const std::string& extension) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = text(name);
    panel.canCreateDirectories = YES;
    if (UTType* type = [UTType typeWithFilenameExtension:text(extension)]) panel.allowedContentTypes = @[ type ];
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return std::nullopt;
    return std::filesystem::path(panel.URL.path.UTF8String);
}

std::string clipboard() {
    NSString* value = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    return value ? value.UTF8String : "";
}

void activate() { [NSApp activateIgnoringOtherApps:YES]; }

bool singleInstance(const std::string& identifier) {
    NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:[text(identifier) stringByAppendingString:@".lock"]];
    lockDescriptor = open(path.fileSystemRepresentation, O_CREAT | O_RDWR, 0600);
    if (lockDescriptor >= 0 && flock(lockDescriptor, LOCK_EX | LOCK_NB) == 0) return true;
    pid_t self = [NSProcessInfo processInfo].processIdentifier;
    for (NSRunningApplication* other in [NSRunningApplication runningApplicationsWithBundleIdentifier:text(identifier)])
        if (other.processIdentifier != self) {
            [other activateWithOptions:NSApplicationActivateIgnoringOtherApps | NSApplicationActivateAllWindows];
            break;
        }
    return false;
}

}
