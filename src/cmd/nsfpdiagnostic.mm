// nsfpdiagnostic -- Dumps registered NSFileProvider domains to stdout.
// Used for developer diagnostics via the --dump-nsfp-domains CLI flag.
// 2026-03-07: Initial creation (VOD-027).

#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>

#include <iostream>
#include <string>

namespace OCC {

int dumpNSFileProviderDomains()
{
    if (@available(macOS 11.0, *)) {
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block int result = 0;

        [NSFileProviderManager getDomainsWithCompletionHandler:^(NSArray<NSFileProviderDomain *> *domains,
                                                                  NSError *error) {
            if (error) {
                std::cerr << "Error querying NSFileProvider domains: "
                          << error.localizedDescription.UTF8String << std::endl;
                result = 1;
                dispatch_semaphore_signal(semaphore);
                return;
            }

            if (domains.count == 0) {
                std::cout << "No NSFileProvider domains registered." << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }

            std::cout << "NSFileProvider Domains:" << std::endl;

            for (NSFileProviderDomain *domain in domains) {
                std::string separator(41, '-');
                std::cout << separator << std::endl;

                std::cout << "Identifier:   "
                          << (domain.identifier ? domain.identifier.UTF8String : "(null)")
                          << std::endl;

                std::cout << "Display Name: "
                          << (domain.displayName ? domain.displayName.UTF8String : "(null)")
                          << std::endl;

                // Retrieve the user-visible URL for this domain's root.
                NSFileProviderManager *manager =
                    [NSFileProviderManager managerForDomain:domain];
                if (manager) {
                    dispatch_semaphore_t urlSemaphore = dispatch_semaphore_create(0);
                    __block NSString *pathString = nil;

                    [manager getUserVisibleURLForItemIdentifier:NSFileProviderRootContainerItemIdentifier
                                             completionHandler:^(NSURL *url, NSError *urlError) {
                        if (url) {
                            pathString = [url.path copy];
                        } else if (urlError) {
                            pathString = [NSString stringWithFormat:@"(error: %@)",
                                          urlError.localizedDescription];
                        }
                        dispatch_semaphore_signal(urlSemaphore);
                    }];

                    dispatch_semaphore_wait(urlSemaphore, dispatch_time(DISPATCH_TIME_NOW,
                                                                        (int64_t)(5 * NSEC_PER_SEC)));

                    std::cout << "Path:         "
                              << (pathString ? pathString.UTF8String : "(unavailable)")
                              << std::endl;
                } else {
                    std::cout << "Path:         (no manager available)" << std::endl;
                }
            }

            std::string separator(41, '-');
            std::cout << separator << std::endl;
            std::cout << "Total: " << domains.count << " domain(s)" << std::endl;

            dispatch_semaphore_signal(semaphore);
        }];

        // Wait up to 30 seconds for the async query to complete.
        dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW,
                                                          (int64_t)(30 * NSEC_PER_SEC)));
        return result;
    } else {
        std::cerr << "NSFileProvider domains require macOS 11.0 or later." << std::endl;
        return 1;
    }
}

} // namespace OCC
