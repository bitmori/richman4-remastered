// SPDX-License-Identifier: GPL-3.0-or-later
// Dev-only XCUITest (cmake -DRICH4_IOS_UITESTS=ON): drives the real app with real simulator
// touches through menu -> new-game setup -> board, so touch-to-640x480 mapping (letterbox, notch,
// SDL_ConvertEventToRenderCoordinates) is exercised end to end. Screens are attached to the
// .xcresult. Not part of the normal build.
#import <XCTest/XCTest.h>

@interface Rich4TouchTests : XCTestCase
@end

@implementation Rich4TouchTests

// Taps a point given in the game's 640x480 logical coordinates (letterboxed into the window).
- (void)tapApp:(XCUIApplication *)app x:(CGFloat)x y:(CGFloat)y {
    XCUIElement *window = [app.windows elementBoundByIndex:0];
    const CGSize size = window.frame.size;
    const CGFloat scale = MIN(size.width / 640, size.height / 480);
    const CGFloat left = (size.width - 640 * scale) / 2, top = (size.height - 480 * scale) / 2;
    NSLog(@"rich4-uitest: window %@, tap logical (%.0f, %.0f)", NSStringFromCGSize(size), x, y);
    [[window coordinateWithNormalizedOffset:CGVectorMake((left + x * scale) / size.width,
                                                         (top + y * scale) / size.height)] tap];
}

- (void)attach:(NSString *)name {
    XCTAttachment *attachment = [XCTAttachment attachmentWithScreenshot:[XCUIScreen.mainScreen screenshot]];
    attachment.name = name;
    attachment.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:attachment];
}

- (void)testMenuSetupBoardByTouch {
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"io.divby0.rich4"];
    // Simulator apps can write host paths: record inputs.jsonl / events.jsonl for the run.
    app.launchArguments = @[ @"--output", @"/tmp/rich4-ios-out" ];
    [app launch];
    sleep(4);
    [self attach:@"01-menu"];
    [self tapApp:app x:190 y:380]; // START (menus.cpp entry centre)
    sleep(3);
    [self attach:@"02-setup"];
    [self tapApp:app x:44 y:51]; // first character portrait
    sleep(1);
    [self attach:@"03-setup-picked"];
    [self tapApp:app x:496 y:196]; // OK
    for (int i = 0; i < 6; ++i) { // the airplane / parachute intro
        sleep(4);
        [self attach:[NSString stringWithFormat:@"04-intro-%d", i]];
    }
    [self attach:@"05-board"];
    // Background and resume while the human is to roll: SDL_EVENT_WILL_ENTER_BACKGROUND writes
    // saves/Autosave.DAT (check its time stamp), and the game must carry on afterwards.
    NSLog(@"rich4-uitest: backgrounding at %@", NSDate.date);
    [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];
    sleep(3);
    [app activate];
    sleep(2);
    [self attach:@"05-board-resumed"];
    [self tapApp:app x:227 y:154]; // GO (default dice panel position, main_screen.hpp)
    sleep(2);
    [self attach:@"06-rolling"];
    sleep(6);
    [self attach:@"07-after-go"];
}

@end
